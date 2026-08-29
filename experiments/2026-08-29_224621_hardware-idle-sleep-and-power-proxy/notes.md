# Notes

Hardware run on a NUCLEO-H533RE, firmware built from the commit recorded in the
repository at the time of this run. Raw device output is in `data/raw_capture.txt`
and the parsed numbers in `config.json`.

Reproduce with the procedure in [docs/operation_manual.md](../../docs/operation_manual.md):
build, flash, then `python tools/parse_bench.py <port>`.
