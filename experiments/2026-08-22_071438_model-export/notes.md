# Notes

Model export from the baseline checkpoint in
`2026-08-22_071116_dscnn-kws-baseline`. The exporter quantises the graph,
verifies the INT8 reference against the float model before emitting anything,
and regenerates the model, tables, evaluation set, and replay clips under
`KWS_TRON/audio/`. Verification passed 12 of 12 samples.

Reproduce with `cd model && python -m kws.export --checkpoint ../experiments/2026-08-22_071116_dscnn-kws-baseline/checkpoint.pt`,
see [docs/operation_manual.md](../../docs/operation_manual.md) section 6. This
note was added after the run to bring the folder up to the convention in
[experiments/README.md](../README.md).
