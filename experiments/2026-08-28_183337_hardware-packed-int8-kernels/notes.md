# Notes

Release build with the packed SMLAD INT8 kernels in place. INT8 dropped from
160.2 ms scalar to 102.6 ms, now clearly faster than FP32 at 125.1 ms, which
is the ordering every later run reproduces.

Raw device output is in `data/`, parsed numbers in `config.json` and
`results.md`. Reproduce with the procedure in
[docs/operation_manual.md](../../docs/operation_manual.md): build, flash, then
`python tools/parse_bench.py <port>`. This note was added after the run to
bring the folder up to the convention in [experiments/README.md](../README.md).
