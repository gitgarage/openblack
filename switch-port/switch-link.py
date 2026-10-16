#!/usr/bin/env python3
"""The laptop end of openblack's USB-C link when the Switch is plugged straight into the laptop - no tablet, no adb.

With the laptop as USB host the Switch is the device: atlas_usb_link falls back to libnx's usbComms (VID 057e,
PID 3000, one vendor interface, a bulk pipe each way). This program:
  - appends everything the Switch sends (openblack's log) to LOG, as it arrives;
  - serves TCP port 10810 like the tablet's AccessoryLinkService did: what a client writes goes to the Switch,
    and what the Switch sends goes to every client - so switch-port/update.py works with SWITCH_TABLET=127.0.0.1;
  - waits for the Switch, and finds it again after a replug or an openblack restart.

Atmosphère's HTC (enable_htc in sdmc:/atmosphere/config/system_settings.ini) must be off: it takes the Switch's
USB device port (it shows as 057e:3005 "NintendoSDK DevKit") and usbComms can't start.

    switch-port/switch-link.py [LOG]
"""
import os
import socket
import sys
import threading
import time

import usb.core
import usb.util

VID, PID = 0x057E, 0x3000
PORT = 10810
LOG = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                         'logs', time.strftime('%Y-%m-%d'), 'switch-link.log')

clients = []
clients_lock = threading.Lock()
link = {'out': None}  # the Switch's bulk OUT endpoint while connected
link_lock = threading.Lock()


def note(text):
    sys.stderr.write(time.strftime('%H:%M:%S ') + text + '\n')
    sys.stderr.flush()


def to_clients(data):
    with clients_lock:
        for c in list(clients):
            try:
                c.sendall(data)
            except OSError:
                clients.remove(c)


def serve():
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('0.0.0.0', PORT))
    server.listen()
    while True:
        conn, addr = server.accept()
        note(f'client {addr[0]} connected')
        with clients_lock:
            clients.append(conn)
        threading.Thread(target=client_to_switch, args=(conn,), daemon=True).start()


def client_to_switch(conn):
    try:
        while data := conn.recv(16384):
            with link_lock:
                out = link['out']
                if out is None:
                    break
                out.write(data, timeout=5000)
    except (OSError, usb.core.USBError) as e:
        note(f'client closed: {e}')
    with clients_lock:
        if conn in clients:
            clients.remove(conn)
    conn.close()


def run_device(dev, log):
    if dev.is_kernel_driver_active(0):
        dev.detach_kernel_driver(0)
    dev.set_configuration()
    intf = dev.get_active_configuration()[(0, 0)]
    ep_in = usb.util.find_descriptor(intf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)
    ep_out = usb.util.find_descriptor(intf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
    usb.util.claim_interface(dev, intf)
    with link_lock:
        link['out'] = ep_out
    note('Switch connected')
    try:
        while True:
            try:
                data = bytes(ep_in.read(16384, timeout=1000))
            except usb.core.USBTimeoutError:
                continue
            if data:
                log.write(data)
                log.flush()
                to_clients(data)
    finally:
        with link_lock:
            link['out'] = None
        usb.util.dispose_resources(dev)


def main():
    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    threading.Thread(target=serve, daemon=True).start()
    note(f'logging to {LOG}; waiting for the Switch ({VID:04x}:{PID:04x}); clients on port {PORT}')
    with open(LOG, 'ab') as log:
        while True:
            dev = usb.core.find(idVendor=VID, idProduct=PID)
            if dev is None:
                time.sleep(1)
                continue
            try:
                run_device(dev, log)
            except usb.core.USBError as e:
                note(f'Switch gone: {e}')
                time.sleep(1)


if __name__ == '__main__':
    main()
