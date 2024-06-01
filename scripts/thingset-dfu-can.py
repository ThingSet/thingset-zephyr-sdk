#!/bin/python3
#
# Copyright (c) The ThingSet Project Contributors
#
# SPDX-License-Identifier: Apache-2.0

import os
import socket
import time
import argparse

from datetime import datetime
from progress.bar import Bar

parser = argparse.ArgumentParser(description='ThingSet CAN DFU tool.')
parser.add_argument('filename', help='Path to Zephyr .bin file to flash')
parser.add_argument('-c', '--can', default='can0', help='CAN interface')
parser.add_argument('-t', '--target', default=0xa0, type=lambda x: int(x,0),
    help='Target CAN node address')
parser.add_argument('-s', '--source', default=0x00, type=lambda x: int(x,0),
    help='Source CAN node address')
parser.add_argument('-tb', '--target-bus', default=0x0, type=lambda x: int(x,0),
    help='Target CAN bus number')
parser.add_argument('-sb', '--source-bus', default=0x0, type=lambda x: int(x,0),
    help='Source CAN bus number')

args = parser.parse_args()
bin_file = args.filename

if (args.target < 0x01 or args.target > 0xFD):
    print('Target addresses must be between 0x01 and 0xFD')
    exit(-1)

if (args.source < 0x00 or args.source > 0xFD):
    print('Source addresses must be between 0x00 and 0xFD')
    exit(-1)

if (args.target_bus < 0x0 or args.target_bus > 0xF):
    print('Target bus must be between 0x0 and 0xF')
    exit(-1)

if (args.source_bus < 0x0 or args.source_bus > 0xF):
    print('Source bus must be between 0x0 and 0xF')
    exit(-1)

page_size = 256

CAN_EFF_FLAG = 0x80000000

host2dev = 0x18000000 | CAN_EFF_FLAG | \
    (args.target_bus << 20) | (args.source_bus << 16) | \
    (args.target << 8) | (args.source)

dev2host = 0x18000000 | CAN_EFF_FLAG | \
    (args.source_bus << 20) | (args.target_bus << 16) | \
    (args.source << 8) | (args.target)

s = socket.socket(socket.AF_CAN, socket.SOCK_DGRAM, socket.CAN_ISOTP)
s.bind((args.can, dev2host, host2dev))
s.settimeout(3)

def ts_request(req):
    try:
        sent_bytes = s.send(req)
        if (sent_bytes == len(req)):
            recv_bytes = s.recv(4095)
            return recv_bytes[0]
    except TimeoutError:
        print("Could not connect to device, socket timed out.")
        return 0xC3     # ThingSet error code: Service Unavailable

start = datetime.now()

print("Initializing DFU")
code = ts_request(b"\x02\x19\x02\xD0\x80")  # !DFU/xInit []
if code != 0x84:
    print("Initializing DFU failed with code 0x%X" % code)
    exit()

with open(bin_file, "rb") as f:
    offset = 0
    size = os.path.getsize(bin_file) / 1024
    progress = Bar("Flashing", max=size, suffix="%(index)d/%(max)d KiB = %(percent)d%%")
    while True:
        buf = f.read(page_size)
        if not buf:
            break
        command  = b"\x02\x19\x02\xD1"      # exec node 0xD2 !DFU/xWrite
        command += b"\x81"                  # array with one element
        command += b"\x59" + len(buf).to_bytes(2, "big")
        command += buf

        for tries in range(5):
            sent_bytes = s.send(command)
            if (sent_bytes == len(command)):
                recv_bytes = s.recv(4095)
                # check ThingSet status code and errno returned returned in payload
                if recv_bytes[0] == 0x84 and recv_bytes[2] == 0x00:
                    offset += page_size
                    progress.next(n=(len(buf) / 1024))
                    break
                else:
                    print("Firmware upgrade failed")
                    print(recv_bytes)
        else:
            print("\nConnection timed out. Failed to send all data.")
            exit()

        time.sleep(0.01)

print("\nFinishing DFU")
code = ts_request(b"\x02\x19\x02\xD3\x80")  # !DFU/xBoot []
if code != 0x84:
    print("Finishing DFU failed with code 0x%X" % code)
    exit()

stop = datetime.now()

print("Total duration:", stop - start)
