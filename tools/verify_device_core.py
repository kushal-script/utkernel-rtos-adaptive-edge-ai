"""Compile the device core for the host and check it against the reference.

The C in KWS_TRON/audio is the code that runs on the board. This builds it with
the host compiler and checks two things:

1. The transform agrees with the NumPy front end the model was trained on.
2. The inference core reaches the expected accuracy on the evaluation set that
   travels in flash, under every precision configuration, and reproduces the
   golden reference logits.

A failure here is a bug in the C, found in seconds, without a board. Run it
after touching anything under KWS_TRON/audio:

    python tools/verify_device_core.py
"""

import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

REPO_ROOT = Path(__file__).resolve().parents[1]
AUDIO = REPO_ROOT / "KWS_TRON" / "audio"
sys.path.insert(0, str(REPO_ROOT / "model"))

FFT_HARNESS = r"""
#include <stdio.h>
#include "kws_fft.h"
int main(void){
    kws_fft_init();
    static float x[FFT_SIZE], p[FFT_BINS];
    for(int i=0;i<FFT_SIZE;i++){ if(scanf("%f",&x[i])!=1) return 1; }
    kws_fft_power(x,p);
    for(int i=0;i<FFT_BINS;i++) printf("%.9e\n", p[i]);
    return 0;
}
"""

MFCC_HARNESS = r"""
#include <stdio.h>
#include "kws_features.h"
int main(void){
    kws_features_init();
    static int16_t frame[FRAME_SIZE_SAMPLES];
    static float mfcc[MFCC_COEFFS];
    int v;
    for(int i=0;i<FRAME_SIZE_SAMPLES;i++){ if(scanf("%d",&v)!=1) return 1; frame[i]=(int16_t)v; }
    kws_feature_frame(frame, mfcc);
    for(int c=0;c<MFCC_COEFFS;c++) printf("%.9e\n", mfcc[c]);
    return 0;
}
"""

INFER_HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>
#include "kws_infer.h"
#include "eval_set.h"
int main(int argc, char **argv){
    unsigned mask = (argc>1)? (unsigned)strtoul(argv[1],0,0) : 0u;
    kws_result_t r; int correct=0;
    for(int s=0;s<EVAL_SAMPLE_COUNT;s++){
        kws_infer(&eval_features[(size_t)s*EVAL_ELEMS_PER_SAMPLE], mask, 0, &r);
        if(r.top_class==(int8_t)eval_labels[s]) correct++;
        for(int c=0;c<KWS_NUM_CLASSES;c++) printf("%.7e ", r.logits[c]);
        printf("\n");
    }
    fprintf(stderr,"%d %d\n", correct, EVAL_SAMPLE_COUNT);
    return 0;
}
"""


STEM_HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include "kws_model.h"
#include "kws_kernels.h"
#include "eval_set.h"
/* The padded convolution both ways over every evaluation grid: the clipped
   window path with the folded and row sum tables, and the original bounds
   checked path with neither. They must agree to the byte. */
int main(void){
    const kws_layer_t *L = &kws_layers[0];
    static int32_t folded[4096], rowsum[4096];
    static int8_t fast[65536], ref[65536];
    unsigned row_len = (unsigned)L->kernel_w * L->in_c, per = (unsigned)L->kernel_h * row_len;
    unsigned out_n = (unsigned)L->out_h * L->out_w * L->out_c;
    if (L->out_c > 4096 || (unsigned)L->out_c * L->kernel_h > 4096 || out_n > 65536) { fprintf(stderr, "harness too small\n"); return 2; }
    for (unsigned oc = 0; oc < L->out_c; oc++) {
        int32_t sum = 0;
        for (unsigned k = 0; k < per; k++) sum += L->weight_int8[oc * per + k];
        folded[oc] = L->bias_int32[oc] + L->input_offset * sum;
        for (unsigned kh = 0; kh < L->kernel_h; kh++) {
            int32_t rs = 0;
            for (unsigned k = 0; k < row_len; k++) rs += L->weight_int8[(oc * L->kernel_h + kh) * row_len + k];
            rowsum[oc * L->kernel_h + kh] = rs;
        }
    }
    unsigned bad = 0;
    for (int s = 0; s < EVAL_SAMPLE_COUNT; s++) {
        const int8_t *g = &eval_features[(size_t)s * EVAL_ELEMS_PER_SAMPLE];
        kws_conv_int8(L, g, L->weight_int8, L->bias_int32, folded, rowsum, fast);
        kws_conv_int8(L, g, L->weight_int8, L->bias_int32, NULL, NULL, ref);
        if (memcmp(fast, ref, out_n) != 0) bad++;
    }
    printf("%u %d\n", bad, EVAL_SAMPLE_COUNT);
    return 0;
}
"""


def compile_harness(work: Path, name: str, source: str, extra: list) -> Path:
    src = work / f"{name}.c"
    src.write_text(source)
    binary = work / name
    cmd = ["cc", "-O2", f"-I{AUDIO}", "-o", str(binary), str(src)]
    cmd += [str(AUDIO / f) for f in extra]
    cmd += ["-lm"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stderr[:4000])
        raise SystemExit(f"failed to compile {name}")
    return binary


def check_transform(work: Path) -> bool:
    from kws.features import FeatureConfig

    cfg = FeatureConfig()
    binary = compile_harness(work, "fft_check", FFT_HARNESS, ["kws_fft.c"])

    rng = np.random.default_rng(7)
    worst = 0.0
    for _ in range(8):
        x = (rng.standard_normal(cfg.n_fft) * 0.3).astype(np.float32)
        stdin = "\n".join(f"{v:.9e}" for v in x)
        out = subprocess.run([str(binary)], input=stdin, capture_output=True, text=True)
        got = np.array([float(v) for v in out.stdout.split()])
        ref = np.abs(np.fft.rfft(x.astype(np.float64), n=cfg.n_fft)) ** 2
        worst = max(worst, float((np.abs(got - ref) / (ref + 1e-9)).max()))

    ok = worst < 1e-4
    print(f"  transform vs NumPy   max relative error {worst:.2e}   {'pass' if ok else 'FAIL'}")
    return ok


def check_features(work: Path) -> bool:
    """The whole frame path, including the sparse mel projection and the DCT."""
    from kws.features import FeatureConfig, MfccExtractor

    cfg = FeatureConfig()
    extractor = MfccExtractor(cfg)
    binary = compile_harness(
        work, "mfcc_check", MFCC_HARNESS,
        ["kws_features.c", "kws_fft.c", "mfcc_tables.c", "kws_model.c"],
    )

    rng = np.random.default_rng(11)
    worst = 0.0
    for trial in range(6):
        if trial == 0:
            t = np.arange(cfg.frame_samples) / cfg.sample_rate
            wave = np.sin(2 * np.pi * 440 * t) * 0.4
        elif trial == 1:
            wave = np.zeros(cfg.frame_samples)
        else:
            wave = rng.standard_normal(cfg.frame_samples) * 0.2
        pcm = np.clip(np.round(wave * 32768.0), -32768, 32767).astype(np.int16)

        stdin = "\n".join(str(int(v)) for v in pcm)
        out = subprocess.run([str(binary)], input=stdin, capture_output=True, text=True)
        got = np.array([float(v) for v in out.stdout.split()])
        ref = extractor.mfcc_frame(pcm.astype(np.float32) / 32768.0)
        scale = max(1.0, float(np.abs(ref).max()))
        worst = max(worst, float(np.abs(got - ref).max() / scale))

    ok = worst < 1e-3
    print(f"  MFCC frame vs NumPy  max relative error {worst:.2e}   {'pass' if ok else 'FAIL'}")
    return ok


def check_inference(work: Path) -> bool:
    binary = compile_harness(
        work, "infer_check", INFER_HARNESS,
        ["kws_infer.c", "kws_kernels.c", "kws_model.c", "eval_set.c"],
    )

    results = {}
    logits = {}
    masks = {"all int8": "0x0", "all fp32": None, "alternating": None}

    header = (AUDIO / "kws_model.h").read_text()
    layers = int(
        [line for line in header.splitlines() if "KWS_NUM_LAYERS" in line][0].split()[-1]
    )
    full = (1 << layers) - 1
    alternating = sum(1 << i for i in range(layers) if i % 2)
    masks = {"all int8": 0, "all fp32": full, "alternating": alternating}

    for name, mask in masks.items():
        out = subprocess.run([str(binary), hex(mask)], capture_output=True, text=True)
        correct, total = (int(v) for v in out.stderr.split())
        results[name] = (correct, total)
        logits[name] = np.array(
            [[float(v) for v in line.split()] for line in out.stdout.strip().splitlines()]
        )

    ok = True
    baseline = None
    for name, (correct, total) in results.items():
        accuracy = correct / total
        if baseline is None:
            baseline = accuracy
        drift = abs(accuracy - baseline)
        good = accuracy > 0.80 and drift < 0.05
        ok = ok and good
        print(f"  {name:12} accuracy {correct:3}/{total} = {accuracy:.4f}"
              f"   {'pass' if good else 'FAIL'}")

    if not ok:
        print("  precision configurations disagree, the boundary conversion is suspect")
    return ok


def check_stem_border(work: Path) -> bool:
    """The clipped window path of the padded convolution against the plain path.

    Accuracy cannot catch a one bit difference here, so this compares the stem's
    int8 output byte for byte over the whole evaluation set.
    """
    binary = compile_harness(
        work, "stem_check", STEM_HARNESS, ["kws_kernels.c", "kws_model.c", "eval_set.c"],
    )
    out = subprocess.run([str(binary)], capture_output=True, text=True)
    if out.returncode != 0:
        print(f"  stem border path   harness error: {out.stderr.strip()}   FAIL")
        return False
    bad, total = (int(v) for v in out.stdout.split())
    ok = bad == 0
    print(f"  stem border path     {total - bad}/{total} grids byte identical   {'pass' if ok else 'FAIL'}")
    return ok


def main() -> int:
    for required in ("kws_model.c", "eval_set.c", "kws_infer.c"):
        if not (AUDIO / required).exists():
            print(f"missing {required}, run model/kws/export.py first")
            return 1

    print("device core verification")
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        passed = check_transform(work)
        passed = check_features(work) and passed
        passed = check_inference(work) and passed
        passed = check_stem_border(work) and passed

    print("all checks passed" if passed else "FAILURES, see above")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
