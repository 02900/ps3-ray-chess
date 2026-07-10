"""pytest fixtures for the RayChess e2e harness.

Run the NETTEST build on RPCS3/PS3, then:

    PS3_IP=127.0.0.1 pytest -v

A single TCP connection is shared across the session (the game serves one client at
a time). Each test starts from a fresh game via the `classic` / `ffa` / `teams`
fixtures, so tests are order-independent.
"""

import os

import pytest

from harness import PS3Client


def pytest_addoption(parser):
    parser.addoption("--ps3-ip", action="store", default=None,
                     help="Console/RPCS3 IP (else PS3_IP env, else 127.0.0.1)")


@pytest.fixture(scope="session")
def client(request):
    host = request.config.getoption("--ps3-ip") or os.environ.get("PS3_IP") or "127.0.0.1"
    c = PS3Client(host=host)
    try:
        c.connect()
    except Exception as e:  # noqa: BLE001
        pytest.skip(f"cannot reach the game at {host}:{c.port} ({e}). "
                    f"Boot the NETTEST build (./scripts/build-test.sh) first.")
    # Sanity: make sure it's actually our server.
    if c.ping() != "pong":
        pytest.skip("connected, but did not get 'pong' — wrong port/app?")
    yield c
    c.close()


@pytest.fixture
def classic(client):
    client.newgame("classic")
    return client


@pytest.fixture
def ffa(client):
    client.newgame("ffa")
    return client


@pytest.fixture
def teams(client):
    client.newgame("teams")
    return client


# Convenience: the standard chess start position, as the `board` reply.
START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR"
