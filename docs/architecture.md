# Architecture

## Thesis

Standard TinyML fixes every optimisation decision at compile time. The window is a constant, feature extraction always runs in full, and the model runs at one precision. The RTOS is a passive scheduler.

This project inverts that. µT-Kernel 3.0 becomes an active co-optimiser. It reads hardware timing from the DWT cycle counter and signal statistics from the capture buffer, and uses them to reshape inference at runtime through native kernel primitives.

## Task pipeline

Five tasks connected by µT-Kernel IPC. Data flows left to right, adaptation decisions flow from T5 back to the upstream tasks.

| Task | Priority | Role | Owns |
| :-- | :-- | :-- | :-- |
| T5 | 2 | Adapt controller | Every adaptation decision and the shared adaptation state; T1 echoes applied resizes into it, and the benchmark pins it |
| T1 | 3 | Ingest | The capture hardware, and the window resize the mailbox carries |
| T2 | 4 | Variance monitor and gate | Block energy, the noise floor, the decision to run the pipeline at all |
| T3 | 5 | Feature extract | Frame scheduling, the quantised grid, the active frame count |
| T4 | 8, or 6 when urgent | Inference engine | The model runner, per layer timing, weight streaming |

Lower is more urgent. T5 leads because a control decision is worth little if it arrives after the work it was meant to shape.

T4 is last, and the urgent tier T5 promotes it to is still below the whole capture chain. That ordering is deliberate: letting a late inference outrank the task that drains the capture buffer would drop audio in order to save a deadline, which is a worse failure than the one being avoided.

## Inter task communication

Every signalling edge is a native kernel primitive, and adaptation values travel as single word shared state alongside the wakeups. Remove the kernel and the adaptive loop collapses, which is the point of the design.

| Primitive | Direction | Purpose |
| :-- | :-- | :-- |
| Event flag `flgid_capture` | Capture interrupt to T2 | A block has filled, set from the DMA interrupt |
| Event flag `flgid_features` | T2 to T3 | The block held speech and is worth turning into features |
| Event flag `flgid_inference` | T3 to T4 | The feature grid is ready to classify |
| Event flag `flgid_control` | T2 and T4 to T5 | Silence, an inference finished, a layer overran |
| Event flag `flgid_gate` | T5 to T3 | The feature gate, peeked with `tk_ref_flg`, never waited on |
| Mailbox `mbxid_window` | T5 to T1 | Window resize and active frame count |
| Memory pool `mplid_layer` | T4 internal | Per layer weight streaming with `tk_get_mpl` |
| Priority change | T5 to T4 | Urgency driven scheduling with `tk_chg_pri` |

One flag object per consumer edge, never shared. With `TA_WMUL` and a `TWF_BITCLR` wait, the kernel stops releasing waiters as soon as one of them clears the pattern, so two tasks waiting on the same object can lose a wakeup. Separate objects remove the hazard and cost nothing, the kernel allows sixteen.

All objects are created once in `usermain` before any task starts, from `app/ipc_objects.c`.

## Data path

```
 replay corpus in flash ── GPDMA ──► capture buffer ──► ring ──► feature grid ──► model
        or INMP441            │        two halves       T2        49 by 10        T4
                              │
                    interrupt sets an event flag
```

The DMA writes one half of the capture buffer while the CPU reads the other, so that handoff costs no copy. The transfer complete interrupt sets an event flag and returns, which is the only work done at interrupt level. The ring between T2 and T3 exists because an analysis frame is longer than one capture block and must not be torn across a refill.

The STM32H5 has no data cache, so DMA and CPU observe the same memory without flush or invalidate. Buffer sizing is in [hardware.md](hardware.md), the source itself in [signal_source.md](signal_source.md).

## File map

```
app/
  app_main.c        usermain, the task table
  app_config.h      every tunable in one place
  app_tasks.h       task identifiers, so T5 can change a priority
  ipc_objects.*     all kernel objects and the shared adaptation state
  signal_source.*   replay and microphone behind one interface
  sample_ring.*     the buffer between capture and features
  t1_ingest.*       capture ownership and window resize
  t2_variance.*     energy, the noise floor, the gate
  t3_features.*     frame scheduling and the quantised grid
  t4_inference.*    the RTOS wrapper around the core
  t5_controller.*   the adaptation policy
audio/
  mfcc_config.h     feature geometry, mirrors the host front end
  kws_fft.*         the transform
  kws_features.*    one MFCC frame, kernel free so a host can check it
  kws_kernels.*     INT8 and FP32 layer kernels
  kws_infer.*       the kernel free model runner
  kws_layer.h       the layer descriptor format
  kws_model.*       generated, weights and the layer table
  mfcc_tables.*     generated, window, filterbank, DCT
  eval_set.*        generated, labelled features for on device scoring
  replay_data.*     generated, waveform clips for the replay source
benchmark/
  dwt_logger.*      the cycle counter
  bench_harness.*   the static against adaptive comparison
```

Everything under `audio/` marked generated comes from `model/kws/export.py` and should never be edited by hand. See [../model/README.md](../model/README.md).
