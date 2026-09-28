# µT-Kernel RTOS-Coupled Adaptive Edge AI

Keyword spotting on the STM32H533RE (Cortex-M33, 250 MHz) where µT-Kernel 3.0 actively co-optimises inference at runtime instead of merely scheduling a static model. Every cycle the kernel reads the DWT cycle counter and live signal statistics, then reshapes how the model runs: capture window size, feature gating, task priority, and per layer numeric precision. The kernel and the model form a closed feedback loop.

TRON Programming Contest 2026, RTOS Application (Students). Board: NUCLEO-H533RE.

## Status

The complete five task pipeline runs on the board, measured with the DWT cycle counter at 250 MHz. The deadline is the classification period itself, 120 ms, derived from the six frame inference stride at a 20 ms hop rather than chosen with knowledge of the measured costs:

| Configuration | Latency | 120 ms deadline | Core accuracy | Core idle |
| :-- | --: | :-- | --: | --: |
| FP32 static | 126.7 ms | missed | 94.0 percent | 23.6 percent |
| INT8 static | 93.4 ms | met | 94.0 percent | 42.5 percent |
| **Adaptive** | **86.1 ms** | **met** | **94.0 percent** | **49.8 percent** |

The adaptive point is faster than the best static compile, not a compromise between the two. It is a mixed precision mask no single precision build can express, and the controller finds it from measurements it takes itself.

The controller calibrates on its first two inferences, measuring what every layer costs in each precision on this silicon, then ranks layers by that measurement rather than by index. On this part the four depthwise layers are together 40 percent slower in INT8, between 33 and 45 percent depending on the layer, because their kernels are scalar while every other layer uses packed multiply accumulate and is 44 percent faster, so the cost optimum is mixed. From full FP32 the controller reaches it in six cost reducing demotions; restarted from all INT8 it climbs back to the same mask in four promotions. Converging to the same operating point from both extremes is what makes it a property of the silicon rather than of where the search began.

Two accuracy figures are reported and they are not the same measurement. The 94.0 percent above is **core accuracy on pre computed feature grids**, which exercises the inference core and nothing upstream of it. End to end accuracy, through capture, the gate, features, and inference, is scored separately on device over the live thirty second windows: 77 to 91 classifications per configuration, **98.9 percent for static FP32, 98.8 for static INT8 and 100 adaptive**. It is higher than the core figure because the replay corpus holds three keywords and silence, an easier task than the twelve class evaluation set, so what it establishes is that nothing is lost between capture and classification, not that the pipeline beats its model. An earlier build reported 47 to 57 percent here; two defects in how the feature tensor and its ground truth were handed to the classifier accounted for the whole gap, see [docs/benchmarking.md](docs/benchmarking.md).

Power now has a mechanism as well as a number. The kernel idle hook shipped as an empty function, so the idle task spun at 250 MHz and no amount of gating work upstream could ever show up as power. It now sleeps, and with all three configurations driving the whole pipeline over identical thirty second windows the core is asleep 49.8 percent of the time adaptive against 23.6 percent for static FP32, a factor of **2.11**, and above static INT8's 42.5 percent. That ratio is the claim; it is deliberately not converted to milliwatts, and [docs/power.md](docs/power.md) explains why.

The capture chain drops nothing. Earlier reports of thousands of dropped blocks were an artefact of the benchmark suspending the consumer while the DMA kept producing; with the producer paused too, the live phase overrun count is zero.

| Piece | State |
| :-- | :-- |
| Boot, clock, kernel, cycle counter | Working on hardware |
| Signal source, timer paced DMA replay | Verified on hardware, blocks at the expected cadence |
| Feature extraction | Matches the host front end to 6e-6, sparse mel projection |
| Inference core | 94 percent core accuracy, identical to host under every precision mix |
| INT8 kernels | Packed SMLAD with folded offsets, 1.22 times faster than FP32 |
| Adaptation controller | Converges to the same mask from both extremes, beats every static build |
| Trained model | 92.8 percent on twelve class Speech Commands, 23,180 parameters |
| Power | Idle sleep implemented, adaptive leaves the core asleep 2.11 times as long as FP32 |
| Capture | Zero dropped blocks over a live run, block rate matches the timer pacing |

## The signal source, and why there is no microphone in the loop

The contribution is the coupling between the kernel and the model, not the transducer. Accuracy cannot be scored against a live microphone because there is no ground truth, so evaluation needs labelled audio replayed through the capture path regardless.

The default source therefore streams a labelled corpus from flash through GPDMA, paced by a timer at the sample rate. It is not a simulation of the capture path, it is the capture path: same DMA channel behaviour, same interrupt per filled block, same event flag into the same task. Only the origin of the bytes differs, and the corpus carries labels so the device can score itself. An INMP441 sits behind the same interface for when a live demonstration is wanted. See [docs/signal_source.md](docs/signal_source.md).

## Repository layout

| Path | Purpose |
| :-- | :-- |
| `KWS_TRON/app/` | The five tasks, kernel objects, signal source |
| `KWS_TRON/audio/` | Transform, layer kernels, inference core, generated model |
| `KWS_TRON/benchmark/` | Cycle counter and the on device benchmark |
| `KWS_TRON/mtk3/` | Vendored µT-Kernel 3.0 BSP2 |
| `desktop/` | The same five tasks on macOS, Linux and Windows, no board needed |
| `model/kws/` | Training, quantisation, export, and the golden reference |
| `docs/` | Design and rationale |
| `experiments/` | Timestamped runs, every number traces back to one |
| `tools/` | Host side verification and plotting |
| `mtk3bsp2_stm32h533.zip` | The firmware, everything under `KWS_TRON/`, as one archive that unpacks to `mtk3bsp2_stm32h533/` |

## Documentation

* [Architecture](docs/architecture.md), the task graph and the IPC map
* [Adaptation](docs/adaptation.md), the runtime knobs and what each one costs
* [Signal source](docs/signal_source.md), how samples reach the pipeline
* [Inference core](docs/inference_core.md), quantisation and precision switching
* [Novelty](docs/novelty.md), the research claim
* [Applications](docs/applications.md), where this transfers, tested rather than asserted, and where it does not
* [Hardware](docs/hardware.md), board, clock tree, memory
* [Benchmarking](docs/benchmarking.md), how the numbers are produced
* [Power](docs/power.md), the mechanism, the measurement, and what is not claimed
* [Operation manual](docs/operation_manual.md), build, flash, and reproduce
* [Desktop program](desktop/README.md), the pipeline without a board, and the limits of what a host run can claim
* [Roadmap](docs/roadmap.md), milestones and what each still owes
* [Introduction slides](docs/TRON2026_intro_slides.pptx), the contest deck, with its content in [slides_outline.md](docs/slides_outline.md)

## Build and run

No external hardware is needed. The audio the pipeline classifies travels with the firmware, so the board on its own reproduces every number here.

```
cmake -S KWS_TRON -B build/Release -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$PWD/KWS_TRON/cmake/arm-none-eabi-gcc.cmake"
cmake --build build/Release
STM32_Programmer_CLI -c port=SWD -w build/Release/KWS_TRON.hex -v -rst
python tools/parse_bench.py /dev/tty.usbmodemXXXX
```

Full procedure, including what every telemetry line means and how to check the software without a board, is in [docs/operation_manual.md](docs/operation_manual.md).

## Run it without a board at all

The same five tasks run on macOS, Linux and Windows, from the same sources, on a host implementation of the µT-Kernel primitives:

```
cmake -S desktop -B build/desktop -DCMAKE_BUILD_TYPE=Release
cmake --build build/desktop
./build/desktop/kws-desktop converge
```

The controller settles on the same mixed precision mask it settles on in hardware, from both extremes, because it ranks its moves against the per layer costs recorded on the board rather than anything this machine can time. What a desktop run may and may not claim is set out in [desktop/README.md](desktop/README.md), including why no power figure is produced there.

## Train and export the model

```
bash model/fetch_dataset.sh
cd model && python -m kws.train --epochs 30
python -m kws.export --checkpoint ../experiments/<run>/checkpoint.pt
```

Training writes a timestamped folder under `experiments/` with the checkpoint, the accuracy curves, and the accuracy against active frames curve the controller trades along. Export regenerates the model, the tables, the evaluation set, and the replay clips under `KWS_TRON/audio/`, and verifies the quantised graph against the float model before emitting anything.

## Related work

The project sits where real time scheduling, adaptive computation and microcontroller inference meet. Numbers in brackets point to the references below. Every entry is a journal article or a paper in the proceedings of a leading conference in its field, linked to its DOI or its official proceedings page.

**Kernel and timing.** µT-Kernel descends from the TRON project's open real time architecture [1] and from T-Kernel, the kernel of the T-Engine platform [2, 3]. The 120 ms deadline is the classification period itself, the implicit deadline periodic model of Liu and Layland [4]. µT-Kernel schedules by fixed priority with preemption, the setting of response time analysis [5], and every latency here is measured on the target with the DWT cycle counter, the measurement based end of the timing analysis methods surveyed by Wilhelm et al. [6].

**Adapting computation to a time budget.** Trading result quality for timeliness is the imprecise computation model [7]. Feedback control scheduling regulates the deadline miss ratio from measurements [8], the elastic task model compresses task rates under overload [9], and PowerDial turns static configuration parameters into knobs that a control loop trades against accuracy [10]. For neural networks, ApNet approximates DNN layers so that deadlines hold on an embedded GPU [11], NeuOS coordinates system level and application level decisions across several DNNs to keep latency predictable [12], and LaLaRAND schedules individual layers across CPU and GPU, quantising the layers it runs on the CPU [13]. MCDNN serves requests with model variants that trade accuracy for resources [14], and Han et al. survey networks that adapt their computation at inference time [15]. On microcontrollers the closest work adapts inference to harvested energy: SONIC and TAILS run DNN inference on intermittently powered devices [16], Zygarde schedules DNN tasks on batteryless systems as imprecise computations [17], and ePerceptive varies inference fidelity with the energy available [18]. Here the loop runs inside the RTOS on the microcontroller itself, acts per layer, and is driven by cycle counts measured on the target, and the point it converges to is a precision mask no static build expresses.

**Speech front end and keyword spotting.** The features are mel frequency cepstral coefficients [19] on the mel scale [20], computed with a Hann window [21], a radix 2 FFT [22] and a DCT [23]. Neural keyword spotting grew from small footprint DNNs [24] and CNNs [25] to residual [26] and temporal convolution [27] models, and streaming inference has been benchmarked on phones [28]. The T2 gate compares short time block energy against a threshold placed above a learned noise floor and holds open through a hangover, in the line of endpoint detection from energy and zero crossing rate [29] and statistical voice activity detection with a hangover scheme [30]. Low power speech recognition chips likewise integrate a neural voice activity detector alongside the recogniser [31].

**Network and training.** The classifier is a DS-CNN, a stack of the depthwise separable convolutions that Xception [32] and MobileNetV2 [33] made standard and that EfficientNet builds on and scales [34]. MnasNet put latency measured on the target phone into its search objective [35], the stance the T5 controller takes at runtime. Training uses batch normalisation [36], folded into the convolutions at export, and label smoothing [37].

**Integer inference and precision.** The INT8 path follows the integer only arithmetic of Jacob et al. [38]: real values mapped through a scale and a zero point, and outputs requantised with a fixed point multiplier. Lin et al. allocate fixed point bit widths layer by layer [39], and Nagel et al. equalise weight ranges across layers so that per tensor INT8 weights lose little accuracy [40]; this project quantises weights per channel instead. Mixed precision gives each layer its own precision: HAQ searches per layer bit widths with feedback from the target hardware [41], HAWQ ranks layers by Hessian sensitivity [42], HAWQ-V3 adds integer only dyadic inference under hardware constraints [43], and Rusci et al. pick per layer bit widths to fit microcontroller memory [44]. Precision that changes at runtime appears in AdaBits [45], any precision networks [46] and Bit-Mixer, which lets any layer change its bit width at test time [47]. These methods fix the policy offline or choose it for accuracy; here the per layer choice between FP32 and INT8 is made online by the RTOS from cycles measured on the silicon.

**Microcontroller inference.** MCUNet codesigns the network and its inference engine for microcontrollers [48], and its successor runs patch by patch to cut peak memory [49]. MicroNets designs TinyML models, keyword spotting among them, for commodity microcontrollers [50], DORY tiles networks across the memory hierarchy with DMA [51], and MLPerf Tiny includes keyword spotting among its benchmarks [52]. TensorFlow Lite Micro interprets a model fixed when it is converted [53], which is why the inference core here is hand written: a layer's precision has to change between inferences.

**Energy and cost structure.** Part of the INT8 gain on this part is moving a quarter of the bytes, and Horowitz's accounting shows why that matters: a memory access costs far more energy than the arithmetic it feeds [54]. Depthwise convolution does little arithmetic per output, nine multiply accumulates here against 64 in a pointwise layer, the low arithmetic intensity regime of the roofline model [55], and Zhang et al. find that existing depthwise and pointwise implementations underuse ARM processors through poor data reuse [56]; the INT8 penalty measured on the four depthwise layers is consistent with that structure. Power is read from idle residency, the quantity dynamic power management acts on [57], and voltage and frequency scaling under deadlines [58] is the complementary lever. Sze et al. survey efficient DNN processing [59] and Zhou et al. survey edge intelligence [60].

## References

1. K. Sakamura, "The TRON project," *IEEE Micro*, vol. 7, no. 2, pp. 8–14, 1987. [doi:10.1109/MM.1987.304835](https://doi.org/10.1109/MM.1987.304835)
2. K. Sakamura and N. Koshizuka, "T-Engine: the open, real-time embedded-systems platform," *IEEE Micro*, vol. 22, no. 6, pp. 48–57, 2002. [doi:10.1109/MM.2002.1134343](https://doi.org/10.1109/MM.2002.1134343)
3. K. Sakamura, "Challenges in the age of ubiquitous computing: a case study of T-Engine, an open development platform for embedded systems," in *Proc. 28th Int. Conf. Software Engineering (ICSE)*, 2006, pp. 713–720. [doi:10.1145/1134285.1134399](https://doi.org/10.1145/1134285.1134399)
4. C. L. Liu and J. W. Layland, "Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment," *Journal of the ACM*, vol. 20, no. 1, pp. 46–61, 1973. [doi:10.1145/321738.321743](https://doi.org/10.1145/321738.321743)
5. M. Joseph and P. Pandya, "Finding Response Times in a Real-Time System," *The Computer Journal*, vol. 29, no. 5, pp. 390–395, 1986. [doi:10.1093/comjnl/29.5.390](https://doi.org/10.1093/comjnl/29.5.390)
6. R. Wilhelm et al., "The worst-case execution-time problem—overview of methods and survey of tools," *ACM Transactions on Embedded Computing Systems*, vol. 7, no. 3, pp. 1–53, 2008. [doi:10.1145/1347375.1347389](https://doi.org/10.1145/1347375.1347389)
7. J. W. S. Liu, W.-K. Shih, K.-J. Lin, R. Bettati, and J.-Y. Chung, "Imprecise computations," *Proceedings of the IEEE*, vol. 82, no. 1, pp. 83–94, 1994. [doi:10.1109/5.259428](https://doi.org/10.1109/5.259428)
8. C. Lu, J. A. Stankovic, S. H. Son, and G. Tao, "Feedback Control Real-Time Scheduling: Framework, Modeling, and Algorithms," *Real-Time Systems*, vol. 23, no. 1–2, pp. 85–126, 2002. [doi:10.1023/A:1015398403337](https://doi.org/10.1023/A:1015398403337)
9. G. C. Buttazzo, G. Lipari, and L. Abeni, "Elastic task model for adaptive rate control," in *Proc. 19th IEEE Real-Time Systems Symposium (RTSS)*, 1998, pp. 286–295. [doi:10.1109/REAL.1998.739754](https://doi.org/10.1109/REAL.1998.739754)
10. H. Hoffmann, S. Sidiroglou, M. Carbin, S. Misailovic, A. Agarwal, and M. Rinard, "Dynamic knobs for responsive power-aware computing," in *Proc. 16th Int. Conf. Architectural Support for Programming Languages and Operating Systems (ASPLOS)*, 2011, pp. 199–212. [doi:10.1145/1950365.1950390](https://doi.org/10.1145/1950365.1950390)
11. S. Bateni and C. Liu, "ApNet: Approximation-Aware Real-Time Neural Network," in *Proc. IEEE Real-Time Systems Symposium (RTSS)*, 2018, pp. 67–79. [doi:10.1109/RTSS.2018.00017](https://doi.org/10.1109/RTSS.2018.00017)
12. S. Bateni and C. Liu, "NeuOS: A Latency-Predictable Multi-Dimensional Optimization Framework for DNN-driven Autonomous Systems," in *Proc. USENIX Annual Technical Conf. (USENIX ATC)*, 2020, pp. 371–385. [Proceedings](https://www.usenix.org/conference/atc20/presentation/bateni)
13. W. Kang, K. Lee, J. Lee, I. Shin, and H. S. Chwa, "LaLaRAND: Flexible Layer-by-Layer CPU/GPU Scheduling for Real-Time DNN Tasks," in *Proc. IEEE Real-Time Systems Symposium (RTSS)*, 2021, pp. 329–341. [doi:10.1109/RTSS52674.2021.00038](https://doi.org/10.1109/RTSS52674.2021.00038)
14. S. Han, H. Shen, M. Philipose, S. Agarwal, A. Wolman, and A. Krishnamurthy, "MCDNN: An Approximation-Based Execution Framework for Deep Stream Processing Under Resource Constraints," in *Proc. 14th Annual Int. Conf. Mobile Systems, Applications, and Services (MobiSys)*, 2016, pp. 123–136. [doi:10.1145/2906388.2906396](https://doi.org/10.1145/2906388.2906396)
15. Y. Han, G. Huang, S. Song, L. Yang, H. Wang, and Y. Wang, "Dynamic Neural Networks: A Survey," *IEEE Transactions on Pattern Analysis and Machine Intelligence*, vol. 44, no. 11, pp. 7436–7456, 2022. [doi:10.1109/TPAMI.2021.3117837](https://doi.org/10.1109/TPAMI.2021.3117837)
16. G. Gobieski, B. Lucia, and N. Beckmann, "Intelligence Beyond the Edge: Inference on Intermittent Embedded Systems," in *Proc. 24th Int. Conf. Architectural Support for Programming Languages and Operating Systems (ASPLOS)*, 2019, pp. 199–213. [doi:10.1145/3297858.3304011](https://doi.org/10.1145/3297858.3304011)
17. B. Islam and S. Nirjon, "Zygarde: Time-Sensitive On-Device Deep Inference and Adaptation on Intermittently-Powered Systems," *Proceedings of the ACM on Interactive, Mobile, Wearable and Ubiquitous Technologies*, vol. 4, no. 3, pp. 1–29, 2020. [doi:10.1145/3411808](https://doi.org/10.1145/3411808)
18. A. Montanari, M. Sharma, D. Jenkus, M. Alloulah, L. Qendro, and F. Kawsar, "ePerceptive: energy reactive embedded intelligence for batteryless sensors," in *Proc. 18th ACM Conf. Embedded Networked Sensor Systems (SenSys)*, 2020, pp. 382–394. [doi:10.1145/3384419.3430782](https://doi.org/10.1145/3384419.3430782)
19. S. Davis and P. Mermelstein, "Comparison of parametric representations for monosyllabic word recognition in continuously spoken sentences," *IEEE Transactions on Acoustics, Speech, and Signal Processing*, vol. 28, no. 4, pp. 357–366, 1980. [doi:10.1109/TASSP.1980.1163420](https://doi.org/10.1109/TASSP.1980.1163420)
20. S. S. Stevens, J. Volkmann, and E. B. Newman, "A Scale for the Measurement of the Psychological Magnitude Pitch," *The Journal of the Acoustical Society of America*, vol. 8, no. 3, pp. 185–190, 1937. [doi:10.1121/1.1915893](https://doi.org/10.1121/1.1915893)
21. F. J. Harris, "On the use of windows for harmonic analysis with the discrete Fourier transform," *Proceedings of the IEEE*, vol. 66, no. 1, pp. 51–83, 1978. [doi:10.1109/PROC.1978.10837](https://doi.org/10.1109/PROC.1978.10837)
22. J. W. Cooley and J. W. Tukey, "An algorithm for the machine calculation of complex Fourier series," *Mathematics of Computation*, vol. 19, no. 90, pp. 297–301, 1965. [doi:10.1090/S0025-5718-1965-0178586-1](https://doi.org/10.1090/S0025-5718-1965-0178586-1)
23. N. Ahmed, T. Natarajan, and K. R. Rao, "Discrete Cosine Transform," *IEEE Transactions on Computers*, vol. C-23, no. 1, pp. 90–93, 1974. [doi:10.1109/T-C.1974.223784](https://doi.org/10.1109/T-C.1974.223784)
24. G. Chen, C. Parada, and G. Heigold, "Small-footprint keyword spotting using deep neural networks," in *Proc. IEEE Int. Conf. Acoustics, Speech and Signal Processing (ICASSP)*, 2014, pp. 4087–4091. [doi:10.1109/ICASSP.2014.6854370](https://doi.org/10.1109/ICASSP.2014.6854370)
25. T. N. Sainath and C. Parada, "Convolutional neural networks for small-footprint keyword spotting," in *Proc. Interspeech*, 2015, pp. 1478–1482. [doi:10.21437/Interspeech.2015-352](https://doi.org/10.21437/Interspeech.2015-352)
26. R. Tang and J. Lin, "Deep Residual Learning for Small-Footprint Keyword Spotting," in *Proc. IEEE Int. Conf. Acoustics, Speech and Signal Processing (ICASSP)*, 2018, pp. 5484–5488. [doi:10.1109/ICASSP.2018.8462688](https://doi.org/10.1109/ICASSP.2018.8462688)
27. S. Choi et al., "Temporal Convolution for Real-Time Keyword Spotting on Mobile Devices," in *Proc. Interspeech*, 2019, pp. 3372–3376. [doi:10.21437/Interspeech.2019-1363](https://doi.org/10.21437/Interspeech.2019-1363)
28. O. Rybakov, N. Kononenko, N. Subrahmanya, M. Visontai, and S. Laurenzo, "Streaming Keyword Spotting on Mobile Devices," in *Proc. Interspeech*, 2020, pp. 2277–2281. [doi:10.21437/Interspeech.2020-1003](https://doi.org/10.21437/Interspeech.2020-1003)
29. L. R. Rabiner and M. R. Sambur, "An Algorithm for Determining the Endpoints of Isolated Utterances," *The Bell System Technical Journal*, vol. 54, no. 2, pp. 297–315, 1975. [doi:10.1002/j.1538-7305.1975.tb02840.x](https://doi.org/10.1002/j.1538-7305.1975.tb02840.x)
30. J. Sohn, N. S. Kim, and W. Sung, "A statistical model-based voice activity detection," *IEEE Signal Processing Letters*, vol. 6, no. 1, pp. 1–3, 1999. [doi:10.1109/97.736233](https://doi.org/10.1109/97.736233)
31. M. Price, J. Glass, and A. P. Chandrakasan, "A Low-Power Speech Recognizer and Voice Activity Detector Using Deep Neural Networks," *IEEE Journal of Solid-State Circuits*, vol. 53, no. 1, pp. 66–75, 2018. [doi:10.1109/JSSC.2017.2752838](https://doi.org/10.1109/JSSC.2017.2752838)
32. F. Chollet, "Xception: Deep Learning with Depthwise Separable Convolutions," in *Proc. IEEE Conf. Computer Vision and Pattern Recognition (CVPR)*, 2017, pp. 1800–1807. [doi:10.1109/CVPR.2017.195](https://doi.org/10.1109/CVPR.2017.195)
33. M. Sandler, A. Howard, M. Zhu, A. Zhmoginov, and L.-C. Chen, "MobileNetV2: Inverted Residuals and Linear Bottlenecks," in *Proc. IEEE/CVF Conf. Computer Vision and Pattern Recognition (CVPR)*, 2018, pp. 4510–4520. [doi:10.1109/CVPR.2018.00474](https://doi.org/10.1109/CVPR.2018.00474)
34. M. Tan and Q. Le, "EfficientNet: Rethinking Model Scaling for Convolutional Neural Networks," in *Proc. 36th Int. Conf. Machine Learning (ICML)*, PMLR 97, 2019, pp. 6105–6114. [Proceedings](https://proceedings.mlr.press/v97/tan19a.html)
35. M. Tan et al., "MnasNet: Platform-Aware Neural Architecture Search for Mobile," in *Proc. IEEE/CVF Conf. Computer Vision and Pattern Recognition (CVPR)*, 2019, pp. 2815–2823. [doi:10.1109/CVPR.2019.00293](https://doi.org/10.1109/CVPR.2019.00293)
36. S. Ioffe and C. Szegedy, "Batch Normalization: Accelerating Deep Network Training by Reducing Internal Covariate Shift," in *Proc. 32nd Int. Conf. Machine Learning (ICML)*, PMLR 37, 2015, pp. 448–456. [Proceedings](https://proceedings.mlr.press/v37/ioffe15.html)
37. C. Szegedy, V. Vanhoucke, S. Ioffe, J. Shlens, and Z. Wojna, "Rethinking the Inception Architecture for Computer Vision," in *Proc. IEEE Conf. Computer Vision and Pattern Recognition (CVPR)*, 2016, pp. 2818–2826. [doi:10.1109/CVPR.2016.308](https://doi.org/10.1109/CVPR.2016.308)
38. B. Jacob et al., "Quantization and Training of Neural Networks for Efficient Integer-Arithmetic-Only Inference," in *Proc. IEEE/CVF Conf. Computer Vision and Pattern Recognition (CVPR)*, 2018, pp. 2704–2713. [doi:10.1109/CVPR.2018.00286](https://doi.org/10.1109/CVPR.2018.00286)
39. D. Lin, S. Talathi, and S. Annapureddy, "Fixed Point Quantization of Deep Convolutional Networks," in *Proc. 33rd Int. Conf. Machine Learning (ICML)*, PMLR 48, 2016, pp. 2849–2858. [Proceedings](https://proceedings.mlr.press/v48/linb16.html)
40. M. Nagel, M. van Baalen, T. Blankevoort, and M. Welling, "Data-Free Quantization Through Weight Equalization and Bias Correction," in *Proc. IEEE/CVF Int. Conf. Computer Vision (ICCV)*, 2019, pp. 1325–1334. [doi:10.1109/ICCV.2019.00141](https://doi.org/10.1109/ICCV.2019.00141)
41. K. Wang, Z. Liu, Y. Lin, J. Lin, and S. Han, "HAQ: Hardware-Aware Automated Quantization With Mixed Precision," in *Proc. IEEE/CVF Conf. Computer Vision and Pattern Recognition (CVPR)*, 2019, pp. 8604–8612. [doi:10.1109/CVPR.2019.00881](https://doi.org/10.1109/CVPR.2019.00881)
42. Z. Dong, Z. Yao, A. Gholami, M. Mahoney, and K. Keutzer, "HAWQ: Hessian AWare Quantization of Neural Networks With Mixed-Precision," in *Proc. IEEE/CVF Int. Conf. Computer Vision (ICCV)*, 2019, pp. 293–302. [doi:10.1109/ICCV.2019.00038](https://doi.org/10.1109/ICCV.2019.00038)
43. Z. Yao et al., "HAWQ-V3: Dyadic Neural Network Quantization," in *Proc. 38th Int. Conf. Machine Learning (ICML)*, PMLR 139, 2021, pp. 11875–11886. [Proceedings](https://proceedings.mlr.press/v139/yao21a.html)
44. M. Rusci, A. Capotondi, and L. Benini, "Memory-Driven Mixed Low Precision Quantization for Enabling Deep Network Inference on Microcontrollers," in *Proc. Machine Learning and Systems (MLSys)*, vol. 2, 2020, pp. 326–335. [Proceedings](https://proceedings.mlsys.org/paper_files/paper/2020/hash/0c8abcf158ed12d0dd94480681186fda-Abstract.html)
45. Q. Jin, L. Yang, and Z. Liao, "AdaBits: Neural Network Quantization With Adaptive Bit-Widths," in *Proc. IEEE/CVF Conf. Computer Vision and Pattern Recognition (CVPR)*, 2020, pp. 2143–2153. [doi:10.1109/CVPR42600.2020.00222](https://doi.org/10.1109/CVPR42600.2020.00222)
46. H. Yu, H. Li, H. Shi, T. S. Huang, and G. Hua, "Any-Precision Deep Neural Networks," in *Proc. AAAI Conf. Artificial Intelligence*, vol. 35, no. 12, 2021, pp. 10763–10771. [doi:10.1609/aaai.v35i12.17286](https://doi.org/10.1609/aaai.v35i12.17286)
47. A. Bulat and G. Tzimiropoulos, "Bit-Mixer: Mixed-precision networks with runtime bit-width selection," in *Proc. IEEE/CVF Int. Conf. Computer Vision (ICCV)*, 2021, pp. 5168–5177. [doi:10.1109/ICCV48922.2021.00514](https://doi.org/10.1109/ICCV48922.2021.00514)
48. J. Lin, W.-M. Chen, Y. Lin, J. Cohn, C. Gan, and S. Han, "MCUNet: Tiny Deep Learning on IoT Devices," in *Advances in Neural Information Processing Systems (NeurIPS)*, vol. 33, 2020, pp. 11711–11722. [Proceedings](https://papers.nips.cc/paper_files/paper/2020/hash/86c51678350f656dcc7f490a43946ee5-Abstract.html)
49. J. Lin, W.-M. Chen, H. Cai, C. Gan, and S. Han, "Memory-efficient Patch-based Inference for Tiny Deep Learning," in *Advances in Neural Information Processing Systems (NeurIPS)*, vol. 34, 2021, pp. 2346–2358. [Proceedings](https://papers.nips.cc/paper_files/paper/2021/hash/1371bccec2447b5aa6d96d2a540fb401-Abstract.html)
50. C. Banbury et al., "MicroNets: Neural Network Architectures for Deploying TinyML Applications on Commodity Microcontrollers," in *Proc. Machine Learning and Systems (MLSys)*, vol. 3, 2021, pp. 517–532. [Proceedings](https://proceedings.mlsys.org/paper_files/paper/2021/hash/c4d41d9619462c534b7b61d1f772385e-Abstract.html)
51. A. Burrello, A. Garofalo, N. Bruschi, G. Tagliavini, D. Rossi, and F. Conti, "DORY: Automatic End-to-End Deployment of Real-World DNNs on Low-Cost IoT MCUs," *IEEE Transactions on Computers*, vol. 70, no. 8, pp. 1253–1268, 2021. [doi:10.1109/TC.2021.3066883](https://doi.org/10.1109/TC.2021.3066883)
52. C. Banbury et al., "MLPerf Tiny Benchmark," in *Proc. Neural Information Processing Systems Track on Datasets and Benchmarks (NeurIPS)*, vol. 1, 2021. [Proceedings](https://datasets-benchmarks-proceedings.neurips.cc/paper_files/paper/2021/hash/da4fb5c6e93e74d3df8527599fa62642-Abstract-round1.html)
53. R. David et al., "TensorFlow Lite Micro: Embedded Machine Learning for TinyML Systems," in *Proc. Machine Learning and Systems (MLSys)*, vol. 3, 2021, pp. 800–811. [Proceedings](https://proceedings.mlsys.org/paper_files/paper/2021/hash/6c44dc73014d66ba49b28d483a8f8b0d-Abstract.html)
54. M. Horowitz, "1.1 Computing's energy problem (and what we can do about it)," in *IEEE Int. Solid-State Circuits Conf. (ISSCC) Digest of Technical Papers*, 2014, pp. 10–14. [doi:10.1109/ISSCC.2014.6757323](https://doi.org/10.1109/ISSCC.2014.6757323)
55. S. Williams, A. Waterman, and D. Patterson, "Roofline: An insightful visual performance model for multicore architectures," *Communications of the ACM*, vol. 52, no. 4, pp. 65–76, 2009. [doi:10.1145/1498765.1498785](https://doi.org/10.1145/1498765.1498785)
56. P. Zhang, E. Lo, and B. Lu, "High Performance Depthwise and Pointwise Convolutions on Mobile Devices," in *Proc. AAAI Conf. Artificial Intelligence*, vol. 34, no. 4, 2020, pp. 6795–6802. [doi:10.1609/aaai.v34i04.6159](https://doi.org/10.1609/aaai.v34i04.6159)
57. L. Benini, A. Bogliolo, and G. De Micheli, "A survey of design techniques for system-level dynamic power management," *IEEE Transactions on Very Large Scale Integration (VLSI) Systems*, vol. 8, no. 3, pp. 299–316, 2000. [doi:10.1109/92.845896](https://doi.org/10.1109/92.845896)
58. P. Pillai and K. G. Shin, "Real-time dynamic voltage scaling for low-power embedded operating systems," in *Proc. 18th ACM Symp. Operating Systems Principles (SOSP)*, 2001, pp. 89–102. [doi:10.1145/502034.502044](https://doi.org/10.1145/502034.502044)
59. V. Sze, Y.-H. Chen, T.-J. Yang, and J. S. Emer, "Efficient Processing of Deep Neural Networks: A Tutorial and Survey," *Proceedings of the IEEE*, vol. 105, no. 12, pp. 2295–2329, 2017. [doi:10.1109/JPROC.2017.2761740](https://doi.org/10.1109/JPROC.2017.2761740)
60. Z. Zhou, X. Chen, E. Li, L. Zeng, K. Luo, and J. Zhang, "Edge Intelligence: Paving the Last Mile of Artificial Intelligence With Edge Computing," *Proceedings of the IEEE*, vol. 107, no. 8, pp. 1738–1762, 2019. [doi:10.1109/JPROC.2019.2918951](https://doi.org/10.1109/JPROC.2019.2918951)

## License

The project's own code is MIT, see [LICENSE](LICENSE). The vendored µT-Kernel 3.0 under `KWS_TRON/mtk3/` is not: the TRON Forum distributes it under the T-License 2.1 and 2.2, stated in each file's header. Every third party component and its licence is listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Intended as a reusable template for real time edge AI on constrained hardware.
