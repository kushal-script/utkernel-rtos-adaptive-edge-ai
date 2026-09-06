# Notes

Baseline DS-CNN training run, 30 epochs on the twelve class Speech Commands
task. Test accuracy 0.9282 with 23,180 parameters; the accuracy against active
frames curve the T5 controller trades along is in `plots/context_curve.png`.
This is the promoted checkpoint behind every model export and every device
number in the project.

Reproduce with `cd model && python -m kws.train --epochs 30` after
`bash model/fetch_dataset.sh`, see [docs/operation_manual.md](../../docs/operation_manual.md)
section 6. This note was added after the run to bring the folder up to the
convention in [experiments/README.md](../README.md); the numbers come from
`results.md` alongside it.
