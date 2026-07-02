#pragma once

/* Audio capture buffer, shared by T1 and the DMA setup in the MSP.
   1024 words = 512 stereo frames = 32 ms at 16 kHz. See docs/hardware.md. */
#define T1_AUDIO_BUFFER_LEN  1024
#define T1_AUDIO_HALF_LEN    (T1_AUDIO_BUFFER_LEN / 2)

/* Mic bring up verification. Set to 0 for normal builds. When 1, a probe task
   streams framed raw capture over UART for tools/check_mic_pcm.py.
   PROBE_SNAP_FRAMES is the snapshot length in stereo frames. */
#define KWS_AUDIO_PROBE      1
#define PROBE_SNAP_FRAMES    8192
