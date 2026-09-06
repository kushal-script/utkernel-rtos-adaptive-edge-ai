# Notes

Longer training run tried against the baseline, same architecture and
parameter count. Test accuracy 0.9200, below the baseline's 0.9282, so the
baseline checkpoint stayed promoted and this run was not used further. Kept
because a rejected variant is still part of the record.

Reproduce with `cd model && python -m kws.train` with the epoch count in
`config.json`. This note was added after the run to bring the folder up to the
convention in [experiments/README.md](../README.md); the numbers come from
`results.md` alongside it.
