# Architecture

## Thesis

Standard TinyML fixes every optimisation decision at compile time. The window is a constant, feature extraction always runs in full, and the model runs at one precision. The RTOS is a passive scheduler.

This project inverts that. µT-Kernel 3.0 becomes an active co-optimiser. It reads hardware timing from the DWT cycle counter and signal statistics from the DMA buffer, and uses them to reshape inference at runtime through native kernel primitives.

## Task pipeline

Five tasks connected by µT-Kernel IPC. Data flows left to right, adaptation decisions flow from T5 back to the upstream tasks.

| Task | Role | Owns |
| :-- | :-- | :-- |
| T1 | DMA ingest | I2S2 plus GPDMA capture from the INMP441, double buffered |
| T2 | Variance monitor and VAD gate | Rolling variance of the capture buffer, silence detection |
| T3 | Feature extract | MFCC, with a bypass path on flat frames |
| T4 | Inference engine | Model runner, precision selection, per layer DWT timing |
| T5 | Adapt controller | Reads timing and signal state, drives window, gating, priority, precision |

## Inter task communication

Every link between tasks uses a native µT-Kernel primitive. Remove the kernel and the adaptive loop collapses, which is the point of the design.

| Primitive | Direction | Purpose |
| :-- | :-- | :-- |
| Event flag `flgid_audio` | T1 ISR to T2 or T3 | Half and full buffer ready, set from DMA interrupt |
| Mailbox | T5 to T1 | Window resize messages |
| Event flag | T5 to T3 | FFT bypass gate |
| Event flag | T4 to T5 | Cycle budget exceeded |
| Priority change | T5 to any | Urgency driven scheduling with `tk_chg_pri` |
| Memory pool | T4 internal | Per layer weight streaming with `tk_get_mpl` |

The IPC objects are declared in one place, `app/ipc_objects.h` and `app/ipc_objects.c`, and created once from `usermain` before any task starts.

## Data path

DMA writes into one half of an SRAM buffer while the CPU processes the other half. The half transfer interrupt sets an event flag atomically, so there is no polling, no copy, and deterministic latency. The STM32H5 has no data cache, so DMA and CPU observe the same memory without flush or invalidate. Buffer sizing and timing are in [hardware.md](hardware.md).

## File map

```
app/
  app_main.c        usermain, task creation table
  ipc_objects.*     all flags, mailboxes, and pools in one place
  t1_dma_ingest.*   capture
  t2_variance.*     variance monitor and VAD gate        (planned)
  t3_features.*     MFCC and bypass gate                 (planned)
  t4_inference.*    model runner, precision, streaming   (planned)
  t5_controller.*   adaptation policy                    (planned)
  audio_probe.*     mic bring up capture over UART
  app_config.h      buffer size, probe flag, tunables
benchmark/
  dwt_logger.*      cycle counting and SWO logging
audio/
  mfcc_config.h     sample rate, frame, mel, FFT settings
  kws_model.h       exported quantised model             (placeholder)
```
