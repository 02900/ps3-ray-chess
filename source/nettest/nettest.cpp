// TCP command server for the e2e test harness. See nettest.h. Compiled only under
// -DNETTEST. The socket skeleton mirrors ps3-remote-play's imgserver.c (netSocket /
// netBind / netListen / netAccept / netPoll on a sysThreadCreate thread); the
// main-thread handshake mirrors its frame_mutex / frame_cond pattern.
#ifdef NETTEST

#include "nettest.h"

#include <net/net.h>
#include <net/poll.h>
#include <sys/thread.h>
#include <sys/mutex.h>
#include <sys/cond.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sysmodule/sysmodule.h>

#include <unistd.h>  // usleep (retry backoff)

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace {

const int CMD_PORT = 9010;

// Command/reply handoff between the socket thread and the main loop.
sys_mutex_t g_mtx;
sys_cond_t  g_cond;
bool        g_mtxOk   = false;
bool        g_condOk  = false;
bool        g_hasCmd  = false;   // a command is waiting for the main loop
bool        g_hasReply = false;  // the main loop posted a reply
std::string g_cmd;
std::string g_reply;

sys_ppu_thread_t g_tid;
volatile bool    g_running = false;
int              g_listen  = -1;

// Synthetic pad: buttons pressed this frame. Written and read on the main thread
// only (the `press` command is dispatched inside PopCommand, at the top of a frame,
// and consumed by that same frame's input helpers), so it needs no lock.
std::set<int> g_synth;

// Only one client with one outstanding command at a time, so the critical sections
// are tiny; if the mutex couldn't be created these degrade to no-ops safely.
void lock()   { if (g_mtxOk) sysMutexLock(g_mtx, 0); }
void unlock() { if (g_mtxOk) sysMutexUnlock(g_mtx); }

// Read one '\n'-terminated line (dropping '\r'). Returns false on disconnect/error.
bool recv_line(int sock, std::string& out) {
    out.clear();
    char c;
    for (;;) {
        int n = netRecv(sock, &c, 1, 0);
        if (n <= 0) return false;
        if (c == '\n') return true;
        if (c == '\r') continue;
        if (out.size() < 512) out += c;
    }
}

// Serve one client until it disconnects. Each command is submitted to the main loop
// and this thread parks on g_cond until the reply is posted.
void handle_client(int sock) {
    std::string line;
    while (g_running && recv_line(sock, line)) {
        lock();
        g_cmd = line;
        g_hasCmd = true;
        g_hasReply = false;
        while (g_running && !g_hasReply) {
            if (g_condOk) sysCondWait(g_cond, 100000);  // signal, or 100ms re-check
            else { unlock(); sysThreadYield(); lock(); }
        }
        std::string reply = g_hasReply ? g_reply : std::string("err interrupted");
        g_hasReply = false;
        unlock();

        reply += "\n";
        if (netSend(sock, reply.c_str(), reply.size(), 0) < 0) break;
    }
}

void server_thread(void*) {
    std::printf("[nettest] server thread up\n");

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CMD_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // The net stack can need a moment to be ready after netInitialize (ps3-remote-play
    // happens to create its sockets seconds after boot, we do it immediately). Retry
    // the whole setup until it binds+listens instead of giving up on the first miss.
    g_listen = -1;
    for (int attempt = 1; g_running; attempt++) {
        int s = netSocket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s < 0) {
            std::printf("[nettest] socket failed (errno=%d, try %d)\n", net_errno, attempt);
            usleep(1000 * 1000); continue;
        }
        int one = 1;
        netSetSockOpt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (netBind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::printf("[nettest] bind failed (errno=%d, try %d)\n", net_errno, attempt);
            netClose(s); usleep(1000 * 1000); continue;
        }
        if (netListen(s, 1) < 0) {
            std::printf("[nettest] listen failed (errno=%d, try %d)\n", net_errno, attempt);
            netClose(s); usleep(1000 * 1000); continue;
        }
        g_listen = s;
        std::printf("[nettest] listening on port %d (try %d)\n", CMD_PORT, attempt);
        break;
    }
    if (g_listen < 0) { sysThreadExit(0); return; }

    while (g_running) {
        struct pollfd pfd;
        pfd.fd = g_listen; pfd.events = POLLIN; pfd.revents = 0;
        int r = netPoll(&pfd, 1, 200);
        if (r <= 0 || !(pfd.revents & POLLIN)) continue;

        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int c = netAccept(g_listen, (struct sockaddr*)&cli, &clilen);
        if (c < 0) continue;

        std::printf("[nettest] client connected\n");
        handle_client(c);
        netClose(c);
        std::printf("[nettest] client disconnected\n");

        // Drop any half-submitted command so its stale reply can't leak to the next.
        lock(); g_hasCmd = false; g_hasReply = false; unlock();
    }

    netClose(g_listen);
    sysThreadExit(0);
}

}  // namespace

namespace nettest {

void Start() {
    sysModuleLoad(SYSMODULE_NET);
    int ni = netInitialize();
    // Non-fatal: on some setups the net stack is already up (or returns a benign
    // code); let the socket calls report the real outcome instead of bailing here.
    std::printf("[nettest] netInitialize -> %d\n", ni);

    // NOTE (RPCS3 lv2 quirks, learned from ps3-remote-play): the mutex must be
    // NON-adaptive and the cond's attr_pshared must be SYS_COND_ATTR_PSHARED
    // (0 -> CELL_EINVAL). Getting these wrong makes create fail on RPCS3 — which is
    // why we also degrade gracefully rather than abort if either still fails.
    sys_mutex_attr_t mattr;
    mattr.attr_protocol  = SYS_MUTEX_PROTOCOL_FIFO;
    mattr.attr_recursive = SYS_MUTEX_ATTR_NOT_RECURSIVE;
    mattr.attr_pshared   = SYS_MUTEX_ATTR_NOT_PSHARED;
    mattr.attr_adaptive  = SYS_MUTEX_ATTR_NOT_ADAPTIVE;
    std::strcpy(mattr.name, "nettm");
    g_mtxOk = (sysMutexCreate(&g_mtx, &mattr) == 0);
    if (!g_mtxOk) std::printf("[nettest] mutex create failed (continuing lock-free)\n");

    if (g_mtxOk) {
        sys_cond_attr_t cattr;
        cattr.attr_pshared = SYS_COND_ATTR_PSHARED;
        cattr.flags = 0;
        cattr.key = 0;
        std::strcpy(cattr.name, "netcd");
        g_condOk = (sysCondCreate(&g_cond, g_mtx, &cattr) == 0);
        if (!g_condOk) std::printf("[nettest] cond create failed (polling fallback)\n");
    }

    g_running = true;
    if (sysThreadCreate(&g_tid, server_thread, NULL, 1500, 64 * 1024, 0,
                        (char*) "raychess_nettest") < 0) {
        std::printf("[nettest] thread create failed\n");
        g_running = false;
    }
}

bool PopCommand(std::string& cmd) {
    bool got = false;
    lock();
    if (g_hasCmd && !g_hasReply) {
        cmd = g_cmd;
        g_hasCmd = false;
        got = true;
    }
    unlock();
    return got;
}

void PostReply(const std::string& reply) {
    lock();
    g_reply = reply;
    g_hasReply = true;
    if (g_condOk) sysCondSignal(g_cond);
    unlock();
}

void SynthPress(int button) { g_synth.insert(button); }
bool SynthPressed(int button) { return g_synth.count(button) != 0; }
bool SynthDown(int button)    { return g_synth.count(button) != 0; }
void SynthClear() { g_synth.clear(); }

}  // namespace nettest

#endif  // NETTEST
