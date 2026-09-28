# Microphone bring up

The pipeline runs from the flash replay source by default, and a microphone is not needed for any result the project claims. This describes what to do when one is attached and a live demonstration is wanted.

## Switching the source

Set `KWS_SIGNAL_SOURCE` to `KWS_SOURCE_I2S` in `app/app_config.h` and rebuild. Nothing above the source interface changes. The I2S path claims GPDMA1 channel 0 and the replay path claims channel 1, so the two never contend.

Wiring is in [hardware.md](hardware.md). The INMP441 delivers 24 bit samples in 32 bit slots and the source narrows the completed block to the 16 bit stream the rest of the pipeline expects before any consumer sees it.

## What to check, in order

The replay source is the reference. Anything the microphone path does that the replay path does not is a microphone problem, which makes bring up a comparison rather than a hunt.

Confirm the capture cadence first. Blocks should arrive at the window length divided by 16 kHz, so 16 ms at the default 256 sample window, and `signal_source_overruns` should stay at zero. A cadence that is wrong by a large factor means the I2S clock is wrong, not the microphone.

Then confirm the signal is real. The energy T2 reports should sit near the noise floor in a quiet room and rise clearly when you speak. If it never moves, the data slot is wrong or the microphone's channel select pin is tied the wrong way. If it is pinned high regardless of sound, the slot is picking up the unused channel.

Then confirm the features are sane. A sustained tone should put energy in a stable set of mel bands. `tools/verify_device_core.py` already proves the transform itself is correct, so a feature problem at this point is a capture problem.

Finally confirm classification. Speaking one of the ten keywords should raise that class. Accuracy is not measured this way, it is measured on the labelled evaluation set, because a live microphone has no ground truth.

## Known unknowns

The I2S path has not run on hardware in its current form. The clock choice, the divider rate error, and the transfer shape are recorded in [hardware.md](hardware.md), and the previous revision of this path did reach a working DMA configuration, but the narrowing step and the source interface are new and unproven.
