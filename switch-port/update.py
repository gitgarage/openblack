#!/usr/bin/env python3
"""Sends a new openblack.nro to the running openblack on the Switch, over the USB-C link through the Pixel Tablet,
and has it restart into it - no RCM, no SD mode (the Switch side is switch_update.cpp, which describes the protocol).

    switch-port/update.py [build/src/openblack.nro]

The Switch must be running an openblack that has the updater (from 2026-10-15 on), with its log link up. The
tablet is SWITCH_TABLET (default 10.0.1.48), port 10810.
"""
import os
import secrets
import socket
import sys
import time
import zlib

TABLET = os.environ.get('SWITCH_TABLET', '10.0.1.48')
PORT = 10810
CHUNK = 32 * 1024
REPLY_TIMEOUT = 20  # seconds to wait for each reply before asking again
TRIES = 5


class Link:
    def __init__(self):
        self.sock = socket.create_connection((TABLET, PORT), timeout=10)
        self.buf = b''

    def send(self, data):
        self.sock.sendall(data)

    def wait(self, nonce, timeout):
        """The next "[update] <nonce> ..." line from the Switch's log, as its words after the nonce (None on timeout)."""
        marker = f'[update] {nonce} '.encode()
        deadline = time.monotonic() + timeout
        while True:
            while b'\n' in self.buf:
                line, self.buf = self.buf.split(b'\n', 1)
                at = line.find(marker)
                if at >= 0:
                    return line[at + len(marker):].decode(errors='replace').strip().split()
            left = deadline - time.monotonic()
            if left <= 0:
                return None
            self.sock.settimeout(left)
            try:
                data = self.sock.recv(65536)
            except socket.timeout:
                return None
            if not data:
                raise ConnectionError('the tablet closed the connection')
            self.buf += data


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), 'build/src/openblack.nro')
    data = open(path, 'rb').read()
    crc = zlib.crc32(data)
    nonce = secrets.token_hex(4)
    link = Link()
    # Blank lines first: more than a chunk, to fill out any chunk an earlier transfer left half sent.
    link.send(b'\n' * (CHUNK + 1024))
    for attempt in range(TRIES):
        link.send(f'ATLAS-UPDATE BEGIN {nonce} {len(data)} {crc:08x}\n'.encode())
        reply = link.wait(nonce, REPLY_TIMEOUT)
        if reply and reply[0] == 'ready':
            break
        if reply and reply[0] == 'failed':
            sys.exit(f'the Switch refused: {" ".join(reply[1:])}')
    else:
        sys.exit('no answer from the Switch: is openblack (a build with the updater) running, with its link up?')

    start = time.monotonic()
    offset = 0
    tries = 0
    while offset < len(data):
        piece = data[offset:offset + CHUNK]
        link.send(f'ATLAS-UPDATE CHUNK {nonce} {offset} {len(piece)} {zlib.crc32(piece):08x}\n'.encode() + piece)
        reply = link.wait(nonce, REPLY_TIMEOUT)
        if reply and reply[0] in ('ack', 'nak') and len(reply) > 1:
            got = int(reply[1])
            tries = 0 if got > offset else tries + 1
            offset = got
        else:
            tries += 1
            # Lost in between: fill out whatever the Switch is still waiting for, then send the chunk again.
            link.send(b'\n' * (CHUNK + 1024))
        if tries >= TRIES:
            sys.exit(f'stuck at byte {offset} of {len(data)}')
        seconds = time.monotonic() - start
        print(f'\r{offset * 100 // len(data):3d}%  {offset // 1024} KiB  {offset / 1024 / max(seconds, 0.001):.0f} KiB/s',
              end='', flush=True)
    print()

    link.send(f'ATLAS-UPDATE END {nonce}\n'.encode())
    reply = link.wait(nonce, 60)
    if not reply or reply[0] != 'installed':
        sys.exit(f'not installed: {" ".join(reply) if reply else "no answer"}')
    print(f'installed {len(data)} bytes (crc {crc:08x}) in {time.monotonic() - start:.0f}s; openblack is restarting into it')


if __name__ == '__main__':
    main()
