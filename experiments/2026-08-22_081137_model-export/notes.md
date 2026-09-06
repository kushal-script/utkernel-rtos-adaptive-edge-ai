# Notes

Second export from the same baseline checkpoint as
`2026-08-22_071438_model-export`, rerun with the export settings recorded in
`config.json`. The INT8 reference agreed with the float model on 8 of 8
verification samples.

Reproduce with `cd model && python -m kws.export --checkpoint ../experiments/2026-08-22_071116_dscnn-kws-baseline/checkpoint.pt`,
see [docs/operation_manual.md](../../docs/operation_manual.md) section 6. This
note was added after the run to bring the folder up to the convention in
[experiments/README.md](../README.md).
