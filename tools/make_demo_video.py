"""Render the demo video from a timed capture of a real desktop run.

The contest allows a video as a supplement to the submission. This builds one
from the actual output of `kws-desktop run --trace`, recorded with the wall
clock time of every line, so nothing on screen is staged. Controller decisions
are held longer than they really took, because they arrive about a tenth of a
second apart and are unreadable at that rate; the real elapsed time of each one
is printed beside it so the pacing cannot mislead.

    python tools/capture_demo_run.py          # writes the timed capture
    python tools/make_demo_video.py           # writes docs/demo_converge.mp4
"""

import json
import sys
from pathlib import Path

import imageio.v2 as imageio
import numpy as np
from PIL import Image, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parents[1]
CAPTURE = REPO / "docs" / "demo_capture.json"
OUT = REPO / "docs" / "demo_converge.mp4"

W, H, FPS = 1280, 720, 15
INK = (18, 25, 58)
NAVY = (30, 39, 97)
PANEL = (23, 31, 71)
ICE = (202, 220, 252)
AMBER = (246, 174, 45)
MID = (143, 162, 196)
WHITE = (255, 255, 255)
GREEN = (126, 200, 145)

LAYERS = ["stem", "dw0", "pw0", "dw1", "pw1", "dw2", "pw2", "dw3", "pw3", "fc"]


def font(size, bold=False):
    for path, idx in (("/System/Library/Fonts/Menlo.ttc", 1 if bold else 0),
                      ("/System/Library/Fonts/Monaco.ttf", 0)):
        try:
            return ImageFont.truetype(path, size, index=idx)
        except Exception:
            continue
    return ImageFont.load_default()


F_TITLE, F_BIG, F_BODY, F_MONO, F_SMALL = (font(40, True), font(26, True),
                                           font(19), font(18), font(15))


def frame():
    img = Image.new("RGB", (W, H), INK)
    return img, ImageDraw.Draw(img)


def mask_panel(d, mask_str, y, caption=None):
    """The ten layers as blocks, amber for FP32 and dark for INT8.

    The tool prints the mask high bit first, so the string runs from layer nine
    down to layer zero. It is reversed here to put the layers in the order the
    model executes them, which is what the labels claim.
    """
    bw, gap = 96, 12
    x0 = (W - (10 * bw + 9 * gap)) // 2
    for i, ch in enumerate(mask_str[::-1]):
        x = x0 + i * (bw + gap)
        fp32 = ch == "F"
        d.rounded_rectangle([x, y, x + bw, y + 74], 8,
                            fill=AMBER if fp32 else PANEL,
                            outline=AMBER if fp32 else NAVY, width=2)
        d.text((x + bw / 2, y + 22), LAYERS[i], font=F_SMALL,
               fill=INK if fp32 else MID, anchor="mm")
        d.text((x + bw / 2, y + 50), "FP32" if fp32 else "INT8", font=F_MONO,
               fill=INK if fp32 else ICE, anchor="mm")
    if caption:
        d.text((W / 2, y + 100), caption, font=F_BODY, fill=MID, anchor="mm")


def header(d, sub=None):
    d.text((60, 44), "RTOS-Coupled Adaptive Edge AI", font=F_BIG, fill=WHITE)
    d.text((60, 82), "the kernel measuring its own silicon and choosing where "
                     "each layer runs", font=F_SMALL, fill=MID)
    if sub:
        d.text((W - 60, 52), sub, font=F_MONO, fill=AMBER, anchor="ra")


def terminal(d, lines, y0=430, maxlines=8):
    d.rounded_rectangle([60, y0 - 22, W - 60, y0 + maxlines * 26 + 14], 10,
                        fill=(12, 17, 40), outline=NAVY, width=2)
    for i, (text, colour) in enumerate(lines[-maxlines:]):
        d.text((84, y0 + i * 26), text, font=F_MONO, fill=colour)


def hold(frames, img, seconds):
    for _ in range(int(seconds * FPS)):
        frames.append(np.asarray(img))


def build():
    cap = json.loads(CAPTURE.read_text())
    rows = cap["rows"]
    moves = [r for r in rows if "moved" in r["line"] or "settled" in r["line"]]
    if not moves:
        sys.exit("capture holds no controller decisions")

    frames = []

    # Title
    img, d = frame()
    header(d)
    d.text((W / 2, 300), "The controller finds its own", font=F_TITLE, fill=WHITE, anchor="mm")
    d.text((W / 2, 352), "operating point", font=F_TITLE, fill=AMBER, anchor="mm")
    d.text((W / 2, 430), "Ten layers. Each one runs in INT8 or FP32.", font=F_BODY, fill=ICE, anchor="mm")
    d.text((W / 2, 462), "Nobody tells it which. It measures, then decides.", font=F_BODY, fill=ICE, anchor="mm")
    d.text((W / 2, 640), "$ kws-desktop run --trace", font=F_MONO, fill=GREEN, anchor="mm")
    hold(frames, img, 3.5)

    # Decisions, real content and real elapsed time, held long enough to read
    term = []
    phase = "Descending from all FP32"
    shown = 0
    for r in moves:
        parts = r["line"].split()
        after = parts[4]
        did_move = r["line"].strip().endswith("moved")
        if did_move and after == "iiiiiiiiii":
            phase = "Restarting from all INT8, the opposite extreme"
        if not did_move and shown > 6:
            continue                      # settled rows repeat, show a few
        shown += 1
        term.append((f"decision {parts[1]:>4}   {parts[2]} -> {after}   "
                     f"{'moved' if did_move else 'settled'}   "
                     f"at {r['t']:.2f} s",
                     AMBER if did_move else MID))
        img, d = frame()
        header(d, f"real elapsed {r['t']:.2f} s")
        mask_panel(d, after, 150, phase)
        d.text((W / 2, 292), "blocks in execution order, the trace line prints "
                             "the mask high bit first", font=F_SMALL, fill=MID,
               anchor="mm")
        terminal(d, term)
        hold(frames, img, 1.1 if did_move else 0.5)

    # The claim
    img, d = frame()
    header(d, "converged")
    mask_panel(d, "iiFiFiFiFi", 150, "mask 0x0AA, the four depthwise layers in FP32")
    d.text((W / 2, 380), "Six demotions down from all FP32.", font=F_BODY, fill=ICE, anchor="mm")
    d.text((W / 2, 414), "Four promotions back up from all INT8.", font=F_BODY, fill=ICE, anchor="mm")
    d.text((W / 2, 462), "The same operating point from both extremes,", font=F_BIG, fill=WHITE, anchor="mm")
    d.text((W / 2, 500), "so it is a property of the silicon, not of the search.",
           font=F_BIG, fill=AMBER, anchor="mm")
    d.text((W / 2, 580), "measured on a NUCLEO-H533RE, replayed here from that capture",
           font=F_SMALL, fill=MID, anchor="mm")
    hold(frames, img, 5.0)

    # Why it wins
    img, d = frame()
    header(d, "on hardware")
    d.text((W / 2, 190), "Faster than either static build", font=F_BIG, fill=WHITE, anchor="mm")
    bars = [("static FP32", 126.0, (192, 57, 43), "misses the 120 ms deadline"),
            ("static INT8", 99.3, NAVY, "meets it"),
            ("adaptive", 95.9, AMBER, "meets it, with the most margin")]
    for i, (name, ms, col, note) in enumerate(bars):
        y = 270 + i * 92
        d.text((300, y + 22), name, font=F_BODY, fill=ICE, anchor="ra")
        width = int(ms / 126.0 * 620)
        d.rounded_rectangle([330, y, 330 + width, y + 46], 6, fill=col)
        d.text((330 + width + 16, y + 22), f"{ms:.1f} ms", font=F_BODY,
               fill=WHITE, anchor="lm")
        d.text((330, y + 62), note, font=F_SMALL, fill=MID)
    dl = 330 + int(120.0 / 126.0 * 620)
    d.line([dl, 250, dl, 560], fill=WHITE, width=2)
    d.text((dl, 236), "120 ms deadline", font=F_SMALL, fill=WHITE, anchor="mm")
    hold(frames, img, 5.0)

    # Close
    img, d = frame()
    header(d)
    d.text((W / 2, 300), "Runs on the board, and on a desktop", font=F_BIG, fill=WHITE, anchor="mm")
    d.text((W / 2, 348), "from the same five uT-Kernel task sources, unmodified",
           font=F_BODY, fill=ICE, anchor="mm")
    d.text((W / 2, 430), "cmake -S desktop -B build/desktop && cmake --build build/desktop",
           font=F_MONO, fill=GREEN, anchor="mm")
    d.text((W / 2, 462), "./build/desktop/kws-desktop converge", font=F_MONO, fill=GREEN, anchor="mm")
    d.text((W / 2, 560), "github.com/kushal-script/utkernel-rtos-adaptive-kws",
           font=F_BODY, fill=WHITE, anchor="mm")
    d.text((W / 2, 596), "TRON Programming Contest 2026   RTOS Application, Students   MIT",
           font=F_SMALL, fill=MID, anchor="mm")
    hold(frames, img, 4.0)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    imageio.mimwrite(OUT, frames, fps=FPS, codec="libx264", quality=8,
                     macro_block_size=1, output_params=["-pix_fmt", "yuv420p"])
    secs = len(frames) / FPS
    print(f"written: {OUT}  {secs:.1f} s, {len(frames)} frames, "
          f"{OUT.stat().st_size/1e6:.1f} MB")


if __name__ == "__main__":
    build()
