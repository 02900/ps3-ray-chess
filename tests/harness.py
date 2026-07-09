"""TCP client for the RayChess e2e test harness.

The game (built with `./scripts/build-test.sh`) runs a line-based TCP command server
on port 9010. Each request is one line ("ping", "state", "board", "press cross", ...);
each reply is one line. This client is the thin transport used by the pytest suite and
by the standalone smoke test.
"""

import os
import socket

DEFAULT_PORT = 9010


class PS3Client:
    def __init__(self, host=None, port=DEFAULT_PORT, timeout=5.0):
        self.host = host or os.environ.get("PS3_IP")
        if not self.host:
            raise RuntimeError("Set PS3_IP (the console/RPCS3 IP) or pass host=")
        self.port = int(os.environ.get("PS3_PORT", port))
        self.timeout = timeout
        self.sock = None
        self._buf = b""

    # -- connection -----------------------------------------------------------
    def connect(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(self.timeout)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        s.connect((self.host, self.port))
        self.sock = s
        self._buf = b""
        return self

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def __enter__(self):
        return self.connect()

    def __exit__(self, *exc):
        self.close()

    # -- request/response -----------------------------------------------------
    def send(self, cmd):
        """Send one command line and return the single-line reply (stripped)."""
        if not self.sock:
            self.connect()
        self.sock.sendall((cmd.strip() + "\n").encode("ascii"))
        while b"\n" not in self._buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("server closed the connection")
            self._buf += chunk
        line, self._buf = self._buf.split(b"\n", 1)
        return line.decode("ascii", "replace").strip()

    # -- convenience wrappers -------------------------------------------------
    def ping(self):
        return self.send("ping")

    def state(self):
        """Return the state reply parsed into a dict of key=value pairs."""
        reply = self.send("state")
        out = {}
        for tok in reply.split():
            if "=" in tok:
                k, v = tok.split("=", 1)
                out[k] = v
        return out

    def board(self):
        return self.send("board")

    def press(self, button):
        return self.send("press " + button)


if __name__ == "__main__":
    with PS3Client() as c:
        print("ping ->", c.ping())
        print("state ->", c.state())
        print("board ->", c.board())
