#!/usr/bin/env python3
"""Drive the Mazda 3 cluster bridge without SimTools or the game.

Sends the same text protocol SimTools uses, so it's the quickest way to check
wiring, CAN and gauge calibration before involving the game.

Requires pyserial:  pip install pyserial

Examples:
  python cluster_test.py COM5 --sweep               # needles up and down forever
  python cluster_test.py COM5 --rpm 800 --speed 60  # hold fixed values
  python cluster_test.py COM5 --fuel 25             # fuel gauge to a quarter
  python cluster_test.py COM5 --rpm 800 --lamps     # cycle warning/indicator lamps
  python cluster_test.py COM5 --raw "420 82 40 00 00 01 00 00 00"   # inject a frame
  python cluster_test.py COM5 --raw "-"             # clear injected frames

Close the Arduino Serial Monitor and SimTools first - only one program can
hold the COM port.
"""

import argparse
import math
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required:  pip install pyserial")

# Values here are in the units the firmware expects *before* its scaling, i.e.
# what SimTools would send with the default config. Truck rpm is multiplied by
# RPM_DISPLAY_MULTIPLIER on the board, so 2500 here fills a 7000 rpm dial.
LAMP_KEYS = ["E", "B", "O", "L", "Y", "H", "P"]


def packet(rpm, speed, coolant, throttle, fuel, lamps=None):
    lamps = lamps or {}
    fields = [f"R{rpm:.0f}", f"S{speed:.1f}", f"T{coolant:.0f}", f"A{throttle:.0f}", f"F{fuel:.0f}"]
    fields += [f"{k}{1 if lamps.get(k) else 0}" for k in LAMP_KEYS]
    return ("".join(fields) + ";\n").encode("ascii")


def read_back(port):
    """Echo anything the board prints (debug output, command replies)."""
    data = port.read(port.in_waiting or 0)
    if data:
        sys.stdout.write(data.decode("ascii", errors="replace"))
        sys.stdout.flush()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="serial port, e.g. COM5 or /dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--rate", type=float, default=50, help="packets per second (default 50)")
    ap.add_argument("--sweep", action="store_true", help="sweep rpm and speed up and down")
    ap.add_argument("--rpm", type=float, default=0)
    ap.add_argument("--speed", type=float, default=0, help="km/h")
    ap.add_argument("--coolant", type=float, default=90, help="deg C")
    ap.add_argument("--throttle", type=float, default=0, help="percent")
    ap.add_argument("--fuel", type=float, default=75, help="percent (0 empty .. 100 full)")
    ap.add_argument("--lamps", action="store_true", help="cycle each lamp on for 2 s in turn")
    ap.add_argument("--raw", help="send a '#' command (custom CAN frame) and exit")
    args = ap.parse_args()

    port = serial.Serial(args.port, args.baud, timeout=0)
    # Opening the port resets most Arduinos; wait for setup() and the boot sweep.
    print("waiting for the board to boot...")
    time.sleep(4)
    read_back(port)

    if args.raw:
        port.write(f"#{args.raw}\n".encode("ascii"))
        time.sleep(0.3)
        read_back(port)
        # Keep feeding telemetry so the board stays "live" and you can watch the
        # effect; Ctrl+C to stop. Custom frames persist until reset or "#-".

    period = 1.0 / args.rate
    start = time.time()
    print("sending - Ctrl+C to stop")
    try:
        while True:
            t = time.time() - start
            rpm, speed, fuel = args.rpm, args.speed, args.fuel
            if args.sweep:
                k = (1 - math.cos(t * 2 * math.pi / 8)) / 2  # 8 s full cycle
                rpm, speed, fuel = k * 2800, k * 200, k * 100
            lamps = {}
            if args.lamps:
                lamps[LAMP_KEYS[int(t // 2) % len(LAMP_KEYS)]] = True
            port.write(packet(rpm, speed, args.coolant, args.throttle, fuel, lamps))
            read_back(port)
            time.sleep(period)
    except KeyboardInterrupt:
        pass
    finally:
        port.close()


if __name__ == "__main__":
    main()
