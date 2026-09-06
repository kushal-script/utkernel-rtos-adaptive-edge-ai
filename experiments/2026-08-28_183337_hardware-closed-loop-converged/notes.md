# Notes

First run with the closed adaptation loop live on hardware: the controller
converged rather than oscillating, with the packed INT8 kernels at 102.6 ms
against FP32 at 125.1 ms. Later runs supersede these figures; the run that
backs the published numbers is `2026-08-30_003716_hardware-mask-divergence-fixed`.

Raw device output is in `data/`, parsed numbers in `config.json` and
`results.md`. Reproduce with the procedure in
[docs/operation_manual.md](../../docs/operation_manual.md): build, flash, then
`python tools/parse_bench.py <port>`. This note was added after the run to
bring the folder up to the convention in [experiments/README.md](../README.md).
