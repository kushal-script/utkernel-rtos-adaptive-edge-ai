# Notes

First release build on hardware, O2, with the INT8 kernels still scalar. The
run exposed the problem the packed kernels then solved: INT8 at 160.2 ms was
slower than FP32 at 125.1 ms, because scalar INT8 pays for unpacking without
the SIMD multiply accumulate that justifies it.

Raw device output is in `data/`, parsed numbers in `config.json` and
`results.md`. Reproduce with the procedure in
[docs/operation_manual.md](../../docs/operation_manual.md): build, flash, then
`python tools/parse_bench.py <port>`. This note was added after the run to
bring the folder up to the convention in [experiments/README.md](../README.md).
