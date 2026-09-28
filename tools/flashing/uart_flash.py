"""STM32 ROM bootloader UART client (AN3155): sector erase, write, verify, GO, never option bytes."""
import os, sys, time
import serial

PORT = os.environ.get("KWS_PORT", "/dev/cu.usbmodem1102")
ACK, NACK = 0x79, 0x1F
FLASH_BASE = 0x08000000
SECTOR_SIZE = 8192   # STM32H533 user flash sector

def open_port():
    return serial.Serial(PORT, 115200, parity=serial.PARITY_EVEN,
                         bytesize=8, stopbits=1, timeout=3.0)

def read_byte(s):
    b = s.read(1)
    return b[0] if b else None

def expect_ack(s, what):
    r = read_byte(s)
    if r != ACK:
        raise RuntimeError(f"{what}: expected ACK, got {hex(r) if r is not None else 'timeout'}")

def send_cmd(s, code):
    s.write(bytes([code, code ^ 0xFF])); s.flush()
    expect_ack(s, f"cmd {hex(code)}")

def sync(s):
    """Resync even if the bootloader is stuck mid transaction."""
    s.write(b"\x7f" * 320); s.flush()
    time.sleep(0.6)
    s.reset_input_buffer()
    for i in range(6):
        s.write(b"\x7f"); s.flush()
        r = read_byte(s)
        if r in (ACK, NACK):
            return True
        time.sleep(0.2)
    return False

def get_id(s):
    send_cmd(s, 0x02)
    n = read_byte(s)
    pid = s.read(n + 1)
    expect_ack(s, "get id")
    return int.from_bytes(pid, "big")

def addr_bytes(a):
    b = a.to_bytes(4, "big")
    return b + bytes([b[0] ^ b[1] ^ b[2] ^ b[3]])

def write_chunk(s, addr, data):
    send_cmd(s, 0x31)
    s.write(addr_bytes(addr)); s.flush()
    expect_ack(s, f"write addr {hex(addr)}")
    n = len(data) - 1
    payload = bytes([n]) + data
    csum = 0
    for x in payload: csum ^= x
    s.write(payload + bytes([csum])); s.flush()
    r = read_byte(s)
    if r != ACK:
        raise RuntimeError(f"write data at {hex(addr)}: got {hex(r) if r is not None else 'timeout'}")

def read_chunk(s, addr, count):
    send_cmd(s, 0x11)
    s.write(addr_bytes(addr)); s.flush()
    expect_ack(s, f"read addr {hex(addr)}")
    n = count - 1
    s.write(bytes([n, n ^ 0xFF])); s.flush()
    expect_ack(s, f"read count {count}")
    data = s.read(count)
    if len(data) != count:
        raise RuntimeError(f"short read at {hex(addr)}: {len(data)}/{count}")
    return data

def erase_sectors(s, sectors):
    send_cmd(s, 0x44)
    n = len(sectors) - 1
    payload = n.to_bytes(2, "big")
    for sec in sectors:
        payload += sec.to_bytes(2, "big")
    csum = 0
    for x in payload: csum ^= x
    s.write(payload + bytes([csum])); s.flush()
    old = s.timeout; s.timeout = 30.0
    expect_ack(s, "extended erase")
    s.timeout = old

def go(s, addr):
    send_cmd(s, 0x21)
    s.write(addr_bytes(addr)); s.flush()
    expect_ack(s, f"go {hex(addr)}")

def main():
    image_path = sys.argv[1]
    do_erase = "--erase" in sys.argv
    image = open(image_path, "rb").read()
    print(f"image {len(image)} bytes, sectors 0..{(len(image)-1)//SECTOR_SIZE}")

    s = open_port()
    if not sync(s):
        print("NO BOOTLOADER ACK, aborting before any write"); return 2
    print("bootloader sync OK")
    pid = get_id(s)
    print(f"chip PID = {hex(pid)} (expect 0x474/0x484 family for H5)")

    if do_erase:
        sectors = list(range((len(image) + SECTOR_SIZE - 1) // SECTOR_SIZE))
        print(f"erasing {len(sectors)} image sectors only ...")
        erase_sectors(s, sectors)
        print("erase done")

    t0 = time.time()
    for off in range(0, len(image), 256):
        chunk = image[off:off+256]
        if len(chunk) % 4:
            chunk += b"\xff" * (4 - len(chunk) % 4)
        if all(b == 0xFF for b in chunk):
            continue
        try:
            write_chunk(s, FLASH_BASE + off, chunk)
        except RuntimeError as e:
            print(f"  hiccup at {hex(off)} ({e}), resyncing and retrying once")
            if not sync(s):
                raise RuntimeError("resync after hiccup failed")
            write_chunk(s, FLASH_BASE + off, chunk)
        if off % 16384 == 0:
            print(f"  write {off}/{len(image)}", flush=True)
    print(f"write done in {time.time()-t0:.1f}s, verifying full image ...")

    t0 = time.time()
    for off in range(0, len(image), 256):
        expect = image[off:off+256]
        got = read_chunk(s, FLASH_BASE + off, len(expect))
        if got != expect:
            print(f"VERIFY MISMATCH at offset {hex(off)}"); return 3
        if off % 32768 == 0:
            print(f"  verify {off}/{len(image)}", flush=True)
    print(f"verify done in {time.time()-t0:.1f}s, full image matches")

    go(s, FLASH_BASE)
    print("GO issued, firmware should be booting")
    s.close()
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
