# Mic verification (M1)

Proves the INMP441 produces real, correctly framed audio before any feature or model work is built on top of it. Two pieces, a firmware probe and a host capture tool.

## Firmware probe

`app/audio_probe.c` is a bring up task compiled in only when `KWS_AUDIO_PROBE` is set in `app/app_config.h`. It collects a snapshot of the raw circular capture buffer, `PROBE_SNAP_FRAMES` stereo frames, and streams it over UART, then repeats after a short delay. It reads the same T1 event flags as the rest of the pipeline, so it also exercises the circular DMA path.

Set `KWS_AUDIO_PROBE` to 0 for normal builds. When the probe is on, the heartbeat task keeps toggling LD2 but does not print, so the UART carries only binary snapshot frames.

## Frame format

Little endian, streamed on USART2 at 115200 baud.

```
offset  size  field
0       4     magic "SNAP"
4       1     version, 1
5       1     channels, 2
6       1     bytes per sample, 4
7       1     reserved
8       4     sample rate, 16000
12      4     frame count
16      n     payload, frame_count * channels * int32, left then right
16+n    4     sum, arithmetic sum of the payload words modulo 2^32
```

Each 32 bit word is one I2S slot. The INMP441 drives one slot, the other stays near zero, which is how the host confirms channel framing.

## Host tool

```
source setup_env.sh
python tools/check_mic_pcm.py /dev/tty.usbmodemXXXX
```

Use `serial-port` from `setup_env.sh` to find the device name. The tool resyncs on the magic, checks the sum, de-interleaves the two slots, and prints statistics.

* left rms above the noise floor means the mic is responding
* right rms near zero confirms the mic drives a single slot and the framing is correct
* dc offset near zero confirms no stuck bias

It saves the raw capture and a waveform plot into `experiments/<timestamp>_mic-verify/`.

## Passing M1

Speak or tap near the mic while capturing. The saved waveform should track the sound, the left channel should show clear signal above the noise floor, the right channel should stay near zero, and the dc offset should be small. Once that holds, M1 is done and feature extraction can begin.
