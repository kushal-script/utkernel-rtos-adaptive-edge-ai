# Notes

First boot of the full pipeline on the NUCLEO-H533RE, debug build. The point of
the run was that the system comes up and reports at all: core accuracy already
reads 94.0 on device, matching the host, while the latencies are unoptimised
debug figures and not comparable with anything later.

Raw device output is in `data/`, parsed numbers in `config.json` and
`results.md`. Reproduce with the procedure in
[docs/operation_manual.md](../../docs/operation_manual.md): build, flash, then
`python tools/parse_bench.py <port>`. This note was added after the run to
bring the folder up to the convention in [experiments/README.md](../README.md).
