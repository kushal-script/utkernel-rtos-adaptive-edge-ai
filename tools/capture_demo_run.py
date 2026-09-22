"""Record a real desktop run with the wall clock time of every line.

The demo video is built from this, so what it shows is the program's own
output rather than a reconstruction. Writes docs/demo_capture.json.
"""

import json
import subprocess
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BIN = REPO / "build" / "desktop" / "kws-desktop"
OUT = REPO / "docs" / "demo_capture.json"

CMD = [str(BIN), "run", "--seconds", "14", "--trace"]


def main():
    if not BIN.exists():
        raise SystemExit(f"{BIN} not built, see desktop/README.md")
    started = time.monotonic()
    rows = []
    proc = subprocess.Popen(CMD, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, bufsize=1)
    for line in proc.stdout:
        rows.append({"t": round(time.monotonic() - started, 3),
                     "line": line.rstrip("\n")})
    proc.wait()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps({"command": " ".join([str(BIN.relative_to(REPO))] + CMD[1:]), "rows": rows,
                               "total_s": round(time.monotonic() - started, 2)},
                              indent=1))
    print(f"written: {OUT}  {len(rows)} lines over {rows[-1]['t']:.1f} s")


if __name__ == "__main__":
    main()
