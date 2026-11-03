"""One serial owner, optional reset, finite capture; no radio transmissions."""
import argparse
import time
from pathlib import Path
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', required=True)
parser.add_argument('--seconds', type=int, default=60)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--reset', action='store_true')
args = parser.parse_args()
port = serial.Serial()
port.port = args.port
port.baudrate = 115200
port.timeout = 1
port.dtr = False
port.rts = False
with port:
    if args.reset:
        port.rts = True
        time.sleep(0.1)
        port.rts = False
    end = time.monotonic() + args.seconds
    with args.output.open('ab', buffering=0) as log:
        while time.monotonic() < end:
            data = port.read(4096)
            if data:
                log.write(data)
                print(data.decode('utf-8', 'replace'), end='', flush=True)
