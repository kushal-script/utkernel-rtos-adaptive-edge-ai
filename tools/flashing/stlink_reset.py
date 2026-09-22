"""Reset the ST-LINK over USB to open a fresh debug window.

On some hosts the ST-LINK's SWD side degrades after a few OpenOCD sessions:
the debug port still answers its ID but every register read fails. A USB
device reset clears that without touching the target. It resets the probe
only, never the STM32, and issues no flash or option byte command of any kind.

    python tools/flashing/stlink_reset.py

Waits for the virtual COM port to come back, and prints its name, which can
change across resets. Run this before jump_bootloader.tcl when that script
fails with "Fail reading CTRL/STAT register".
"""

import glob
import sys
import time

import usb.core

VID, PID = 0x0483, 0x3754


def main():
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        print("no ST-LINK on the bus, replug the board")
        return 1
    before = set(glob.glob("/dev/tty.usbmodem*"))
    try:
        dev.reset()
    except usb.core.USBError as err:
        print(f"reset request failed: {err}")
        return 2
    print("reset issued, waiting for the virtual COM port")
    for _ in range(40):
        time.sleep(0.5)
        ports = sorted(glob.glob("/dev/tty.usbmodem*"))
        if ports and usb.core.find(idVendor=VID, idProduct=PID) is not None:
            changed = set(ports) != before
            print(f"back on {ports[-1]}" + (" (name changed)" if changed else ""))
            return 0
    print("port did not come back within 20 s, replug the board")
    return 3


if __name__ == "__main__":
    sys.exit(main())
