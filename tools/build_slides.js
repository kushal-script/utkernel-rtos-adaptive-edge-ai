/* Builds docs/TRON2026_intro_slides.pptx, the contest introduction deck.
   Figures come from tools/make_slide_figures.py, which reads the raw device
   captures under experiments/, so every number on a slide is traceable.

       python tools/make_slide_figures.py
       node tools/build_slides.js

   Requires pptxgenjs. If it is not resolvable, either install it locally or
   point NODE_PATH at a global install. See docs/slides_outline.md. */

const pptxgen = require("pptxgenjs");
const path = require("path");

const REPO = path.resolve(__dirname, "..");
const FIG  = (f) => path.join(REPO, "docs/figures", f);

const NAVY="1E2761", INK="12193A", ICE="CADCFC", AMBER="F6AE2D",
      SLATE="5A6B8C", WHITE="FFFFFF", LIGHT="F4F7FC", MID="8FA2C4", RED="C0392B";
const HEAD="Cambria", BODY="Calibri";
const NOLINE = () => ({ type:"none" });

const pres = new pptxgen();
pres.layout = "LAYOUT_WIDE";                 // 13.3 x 7.5
pres.author = "Kushal Sathyanarayan";
pres.title  = "RTOS-Coupled Adaptive Edge AI";

const W = 13.3, M = 0.7, CW = W - 2*M;       // content width 11.9

function darkSlide() { const s = pres.addSlide(); s.background = { color: NAVY }; return s; }
function lightSlide(title, kicker) {
  const s = pres.addSlide(); s.background = { color: WHITE };
  s.addText(kicker, { x:M, y:0.42, w:CW, h:0.3, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:12, bold:true, color:AMBER, charSpacing:1.4 });
  s.addText(title, { x:M, y:0.72, w:CW, h:0.78, isTextBox:true, margin:0, valign:"top",
      fontFace:HEAD, fontSize:34, bold:true, color:INK });
  return s;
}
function card(s, x, y, w, h, fill) {
  s.addShape(pres.ShapeType.roundRect, { x, y, w, h, rectRadius:0.10,
      fill:{ color: fill || LIGHT }, line: NOLINE() });
}
function body(s, x, y, w, h, text, color, size) {
  s.addText(text, { x, y, w, h, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:size||12.5, color:color||SLATE, lineSpacingMultiple:1.18 });
}
function numCircle(s, x, y, n) {
  s.addShape(pres.ShapeType.ellipse, { x, y, w:0.42, h:0.42, fill:{color:NAVY}, line:NOLINE() });
  s.addText(String(n), { x, y, w:0.42, h:0.42, isTextBox:true, margin:0, align:"center",
      valign:"middle", fontFace:BODY, fontSize:14, bold:true, color:WHITE });
}
function arrow(s, x, y, w, h) {
  s.addShape(pres.ShapeType.rightArrow, { x, y, w, h, fill:{color:MID}, line:NOLINE() });
}

// ============ 1. TITLE ============
{
  const s = darkSlide();
  s.addShape(pres.ShapeType.ellipse, { x:10.4, y:-1.5, w:5.2, h:5.2, fill:{color:"27306E"}, line:NOLINE() });
  s.addShape(pres.ShapeType.ellipse, { x:11.6, y:4.6, w:3.4, h:3.4, fill:{color:"27306E"}, line:NOLINE() });
  s.addText("TRON Programming Contest 2026   ·   RTOS Application, Students",
    { x:M, y:1.25, w:9.4, h:0.32, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:13, bold:true, color:AMBER, charSpacing:1.2 });
  s.addText("RTOS-Coupled\nAdaptive Edge AI",
    { x:M, y:1.85, w:9.6, h:2.1, isTextBox:true, margin:0, valign:"top",
      fontFace:HEAD, fontSize:52, bold:true, color:WHITE, lineSpacingMultiple:0.95 });
  s.addText("The kernel measures its own silicon and reshapes inference to fit a deadline",
    { x:M, y:4.15, w:9.4, h:0.5, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:17, color:ICE });

  const chips=[["98.2 ms","adaptive latency"],["94.0 %","core accuracy"],["120 ms","deadline held"]];
  chips.forEach((c,i)=>{
    const x=M+i*3.1;
    s.addText(c[0], { x, y:5.25, w:2.9, h:0.55, isTextBox:true, margin:0, valign:"top",
        fontFace:HEAD, fontSize:26, bold:true, color:AMBER });
    s.addText(c[1], { x, y:5.82, w:2.9, h:0.32, isTextBox:true, margin:0, valign:"top",
        fontFace:BODY, fontSize:12.5, color:ICE });
  });
  s.addText([
    { text:"NUCLEO-H533RE", options:{bold:true, color:WHITE} },
    { text:"   Cortex-M33 at 250 MHz   ·   µT-Kernel 3.0   ·   keyword spotting   ·   open source, MIT",
      options:{color:MID} },
  ], { x:M, y:6.6, w:11.0, h:0.35, isTextBox:true, margin:0, valign:"top", fontFace:BODY, fontSize:13.5 });
  s.addNotes("Adaptive keyword spotting where the RTOS is an active co-optimiser rather than a scheduler. Every number in this deck is measured on the board.");
}

// ============ 2. THE PROBLEM ============
{
  const s = lightSlide("Every optimisation decision is frozen at compile time", "THE PROBLEM");
  body(s, M, 1.85, 6.0, 1.4,
    "Deploying a model on a microcontroller means fixing every execution choice before the board has seen a single sample. The RTOS runs the model but does not shape it.",
    SLATE, 15);
  s.addText("Fixed at build time, and never revisited", { x:M, y:3.35, w:6.0, h:0.35, isTextBox:true,
    margin:0, valign:"top", fontFace:BODY, fontSize:13, bold:true, color:INK });
  const frozen=["capture window length","whether features are extracted at all","task priority ordering","numeric precision, every layer"];
  frozen.forEach((f,i)=>{
    const y=3.82+i*0.62;
    s.addShape(pres.ShapeType.ellipse, { x:M+0.04, y:y+0.09, w:0.16, h:0.16, fill:{color:AMBER}, line:NOLINE() });
    s.addText(f, { x:M+0.42, y, w:5.5, h:0.38, isTextBox:true, margin:0, valign:"top",
        fontFace:BODY, fontSize:14, color:SLATE });
  });

  const bx=7.35, bw=5.25;
  [["Static FP32","misses the deadline","125.9",RED],
   ["Static INT8","leaves performance unused","101.5",NAVY]].forEach((r,i)=>{
    const y=1.85+i*1.72;
    card(s, bx, y, bw, 1.5, LIGHT);
    s.addText(r[0], { x:bx+0.35, y:y+0.34, w:2.9, h:0.35, isTextBox:true, margin:0, valign:"top",
        fontFace:BODY, fontSize:15, bold:true, color:INK });
    s.addText(r[1], { x:bx+0.35, y:y+0.72, w:3.0, h:0.35, isTextBox:true, margin:0, valign:"top",
        fontFace:BODY, fontSize:13, color:SLATE });
    s.addText([{ text:r[2], options:{fontSize:30} }, { text:" ms", options:{fontSize:17} }],
      { x:bx+bw-2.35, y:y+0.5, w:2.0, h:0.62, isTextBox:true, margin:0, align:"right", valign:"top",
        fontFace:HEAD, bold:true, color:r[3] });
  });

  card(s, bx, 5.29, bw, 1.32, NAVY);
  s.addText("Neither can express the configuration that is actually best.",
    { x:bx+0.35, y:5.62, w:bw-0.7, h:0.7, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:15, bold:true, color:WHITE });
  s.addNotes("The framing: this is not FP32 versus INT8. The best configuration is neither.");
}

// ============ 3. THE IDEA ============
{
  const s = lightSlide("Five tasks, and every link is a kernel primitive", "THE IDEA");
  body(s, M, 1.78, CW, 0.7,
    "µT-Kernel reads the DWT cycle counter and the signal statistics, then writes back into how the model executes. Each of the four levers is a native kernel call, not a library shim.",
    SLATE, 15);

  const tasks=[["T1","DMA ingest"],["T2","variance + gate"],["T3","MFCC features"],["T4","inference"],["T5","controller"]];
  const bw=2.15, gap=0.35, y0=2.68, bh=1.35;
  tasks.forEach((t,i)=>{
    const x=0.55+i*(bw+gap), ctl=(i===4);
    card(s, x, y0, bw, bh, ctl?AMBER:NAVY);
    s.addText(t[0], { x, y:y0+0.2, w:bw, h:0.42, isTextBox:true, margin:0, align:"center", valign:"top",
        fontFace:HEAD, fontSize:22, bold:true, color:ctl?INK:WHITE });
    s.addText(t[1], { x:x+0.1, y:y0+0.7, w:bw-0.2, h:0.45, isTextBox:true, margin:0, align:"center",
        valign:"top", fontFace:BODY, fontSize:12, color:ctl?INK:ICE });
    if(i<4) arrow(s, x+bw+0.05, y0+bh/2-0.11, 0.25, 0.22);
  });

  s.addShape(pres.ShapeType.leftArrow, { x:0.55, y:4.28, w:12.15, h:0.5,
      fill:{color:AMBER}, line:NOLINE() });
  s.addText("T5 measures, then writes back into all four levers",
    { x:0.55, y:4.28, w:12.15, h:0.5, isTextBox:true, margin:0, align:"center", valign:"middle",
      fontFace:BODY, fontSize:13.5, bold:true, color:INK });

  const lev=[["tk_snd_mbx","capture window resize"],["tk_ref_flg","feature gate"],
             ["tk_chg_pri","task urgency"],["tk_get_mpl","layer weight streaming"]];
  const lw=(12.15-3*0.3)/4;
  lev.forEach((l,i)=>{
    const x=0.55+i*(lw+0.3);
    card(s, x, 5.05, lw, 1.15, LIGHT);
    s.addText(l[0], { x:x+0.28, y:5.24, w:lw-0.5, h:0.34, isTextBox:true, margin:0, valign:"top",
        fontFace:BODY, fontSize:15, bold:true, color:NAVY });
    s.addText(l[1], { x:x+0.28, y:5.6, w:lw-0.5, h:0.4, isTextBox:true, margin:0, valign:"top",
        fontFace:BODY, fontSize:12.5, color:SLATE });
  });
  s.addText("Every stage handoff is tk_set_flg and tk_wai_flg. Remove the kernel and the adaptive loop does not degrade, it stops.",
    { x:M, y:6.5, w:CW, h:0.4, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:14, italic:true, color:SLATE });
  s.addNotes("Every arrow in the pipeline is a native uT-Kernel primitive. The RTOS is load bearing, not incidental.");
}

// ============ 4. HEADLINE RESULT ============
{
  const s = lightSlide("The adaptive point is faster than either static build", "MEASURED ON HARDWARE");
  const rows=[["Static FP32","125.9 ms","missed","23.5 %"],
              ["Static INT8","101.5 ms","met","39.9 %"],
              ["Adaptive","98.2 ms","met","42.0 %"]];
  const x0=M, y0=1.95, tw=7.6, rh=0.98;
  const cols=[0.35, 3.05, 4.75, 6.35];
  [["CONFIGURATION",2.6],["LATENCY",1.6],["DEADLINE",1.6],["CORE IDLE",1.3]].forEach((h,i)=>{
    s.addText(h[0], { x:x0+cols[i], y:y0, w:h[1], h:0.3, isTextBox:true, margin:0, valign:"top",
        fontFace:BODY, fontSize:11, bold:true, color:SLATE, charSpacing:1 });
  });
  rows.forEach((r,i)=>{
    const y=y0+0.44+i*rh, win=(i===2);
    card(s, x0, y, tw, rh-0.14, win?NAVY:LIGHT);
    s.addText(r[0], { x:x0+cols[0], y:y+0.26, w:2.6, h:0.4, isTextBox:true, margin:0, valign:"top",
        fontFace:BODY, fontSize:15, bold:win, color:win?WHITE:INK });
    s.addText(r[1], { x:x0+cols[1], y:y+0.2, w:1.7, h:0.46, isTextBox:true, margin:0, valign:"top",
        fontFace:HEAD, fontSize:20, bold:true, color:win?AMBER:INK });
    s.addText(r[2], { x:x0+cols[2], y:y+0.26, w:1.7, h:0.4, isTextBox:true, margin:0, valign:"top",
        fontFace:BODY, fontSize:14, bold:true,
        color:(r[2]==="met")?(win?WHITE:"2C7A4B"):RED });
    s.addText(r[3], { x:x0+cols[3], y:y+0.26, w:1.3, h:0.4, isTextBox:true, margin:0, valign:"top",
        fontFace:BODY, fontSize:14, color:win?ICE:SLATE });
  });

  const px=8.75, pw=3.85, py=y0+0.44, ph=3*rh-0.14;
  card(s, px, py, pw, ph, AMBER);
  s.addText("98.2 ms", { x:px+0.32, y:py+0.55, w:pw-0.64, h:0.9, isTextBox:true, margin:0, valign:"top",
      fontFace:HEAD, fontSize:44, bold:true, color:INK });
  s.addText("faster than the best static compile, at identical accuracy",
    { x:px+0.32, y:py+1.55, w:pw-0.64, h:0.95, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:14, color:"6B4E0A", lineSpacingMultiple:1.18 });

  s.addText("The deadline is the classification period itself, derived from the inference stride, not chosen after seeing the costs.",
    { x:M, y:5.7, w:CW, h:0.4, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:14, color:SLATE });
  s.addText("Core accuracy is 94.0 percent in all three configurations, so the speedup costs nothing.",
    { x:M, y:6.18, w:CW, h:0.4, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:13, italic:true, color:MID });
  s.addNotes("The headline. Adaptive is not a compromise between the two static builds, it beats both.");
}

// ============ 5. WHY MIXED WINS ============
{
  const s = lightSlide("Why a mixed configuration wins", "THE MECHANISM");
  s.addImage({ path:FIG("layer_inversion.png"), x:M, y:1.7, w:11.9, h:3.45 });
  const cy=5.35, cw=3.78, gap=0.28;
  const cards=[["Four depthwise layers","57 percent slower in INT8, their kernels cannot pack",LIGHT,INK,SLATE],
               ["Every other layer","32 percent faster in INT8, packed SXTB16 and SMLAD",LIGHT,INK,SLATE],
               ["So the optimum is mixed","no single precision build can express it",NAVY,AMBER,ICE]];
  cards.forEach((c,i)=>{
    const x=M+i*(cw+gap);
    card(s, x, cy, cw, 1.4, c[2]);
    s.addText(c[0], { x:x+0.28, y:cy+0.2, w:cw-0.5, h:0.34, isTextBox:true, margin:0, valign:"top",
        fontFace:BODY, fontSize:13.5, bold:true, color:c[3] });
    body(s, x+0.28, cy+0.58, cw-0.5, 0.7, c[1], c[4], 12.5);
  });
  s.addText("The fully connected layer is a few thousand cycles, invisible at this scale.",
    { x:M, y:6.88, w:CW, h:0.32, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:11, italic:true, color:MID });
  s.addNotes("This asymmetry is the whole reason a mixed operating point exists. It was measured, not assumed.");
}

// ============ 6. THE KERNEL FINDS IT ============
{
  const s = lightSlide("The kernel finds it by measuring, not by being told", "THE CLOSED LOOP");
  s.addImage({ path:FIG("convergence.png"), x:5.15, y:1.7, w:7.45, h:3.3 });
  const items=[["Calibrate","T4 runs both pure precisions on its first two inferences and records what every layer costs"],
               ["Rank","T5 ranks layers by that measured delta, never by index"],
               ["Hill climb","every accepted move strictly reduces cost, so the walk cannot cycle"]];
  items.forEach((it,i)=>{
    const y=1.85+i*1.14;
    numCircle(s, M, y, i+1);
    s.addText(it[0], { x:M+0.6, y:y-0.02, w:3.4, h:0.34, isTextBox:true, margin:0, valign:"top",
        fontFace:BODY, fontSize:15, bold:true, color:INK });
    body(s, M+0.6, y+0.34, 3.6, 0.78, it[1], SLATE, 12.5);
  });
  card(s, M, 5.35, CW, 1.42, NAVY);
  s.addText([
    { text:"6 demotions", options:{bold:true, color:AMBER} },
    { text:" from all FP32      ", options:{color:ICE} },
    { text:"4 promotions", options:{bold:true, color:AMBER} },
    { text:" restarting from all INT8      ", options:{color:ICE} },
    { text:"same mask both times", options:{bold:true, color:WHITE} },
  ], { x:M+0.35, y:5.6, w:CW-0.7, h:0.4, isTextBox:true, margin:0, valign:"top", fontFace:BODY, fontSize:15 });
  s.addText("Converging to the same point from both extremes makes it a property of the silicon, not of where the search began.",
    { x:M+0.35, y:6.08, w:CW-0.7, h:0.4, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:13, italic:true, color:ICE });
  s.addNotes("Path independence is the strongest form this claim can take.");
}

// ============ 7. WHAT THE MEASUREMENTS REFUTED ============
{
  const s = lightSlide("What the measurements refuted", "METHOD");
  const steps=[["Predicted","INT8 is 3 to 4 times faster, from an ALU cycle count argument",LIGHT,INK,SLATE],
               ["Measured","28 percent SLOWER on the first hardware run","FBE8E6",RED,"8A3B32"],
               ["Diagnosed","scalar INT8 pays a per element offset add and a 64 bit requantisation while FP32 rides the FPU",LIGHT,INK,SLATE],
               ["Rewritten","folded offsets, packed SXTB16 and SMLAD, bit identical against the golden reference",LIGHT,INK,SLATE],
               ["Result","1.22 times faster, and the depthwise asymmetry that makes the whole contribution visible","FDF0D5","8A6100","6B4E0A"]];
  const bw=2.28, gap=0.19, y=2.15, bh=2.15;
  steps.forEach((st,i)=>{
    const x=M+i*(bw+gap);
    card(s, x, y, bw, bh, st[2]);
    s.addText(st[0], { x:x+0.24, y:y+0.26, w:bw-0.48, h:0.36, isTextBox:true, margin:0, valign:"top",
        fontFace:BODY, fontSize:14, bold:true, color:st[3] });
    body(s, x+0.24, y+0.72, bw-0.48, 1.35, st[1], st[4], 11.5);
    if(i<4) arrow(s, x+bw+0.01, y+bh/2-0.09, 0.17, 0.18);
  });
  card(s, M, 4.85, CW, 1.62, NAVY);
  s.addText("The refutation is what produced the real result.",
    { x:M+0.35, y:5.16, w:CW-0.7, h:0.46, isTextBox:true, margin:0, valign:"top",
      fontFace:HEAD, fontSize:20, bold:true, color:WHITE });
  s.addText("Also corrected: this part has no SMPS, so the power measurement point is the IDD jumper, not an SMPS as the program plan stated.",
    { x:M+0.35, y:5.74, w:CW-0.7, h:0.5, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:13, color:ICE });
  s.addNotes("A judge who finds an error themselves reads it as a mistake. One you hand them reads it as method.");
}

// ============ 8. THREE CHARACTERISTICS ============
{
  const s = lightSlide("The three characteristics the contest rewards", "RESULTS");
  const cards=[["Real time","The deadline is derived from the application, not tuned to the answer. Under live pipeline load the converged mask ran every traced inference at 101 ms, while static FP32 was over on all 45 of its.","120 ms","held under live load"],
               ["Power","The kernel idle hook shipped empty, so the core spun at 250 MHz between blocks. It now sleeps under BASEPRI. Identical audio, identical windows.","1.79×","longer asleep than FP32"],
               ["Footprint","Layer weights are streamed through tk_get_mpl so only the working layer occupies SRAM, with peak pool use measured rather than asserted.","117 KB","of 272 KB SRAM"]];
  const cw=3.78, gap=0.28, y=1.9, ch=4.42;
  cards.forEach((c,i)=>{
    const x=M+i*(cw+gap);
    card(s, x, y, cw, ch, LIGHT);
    numCircle(s, x+0.3, y+0.34, i+1);
    s.addText(c[0], { x:x+0.88, y:y+0.38, w:cw-1.15, h:0.4, isTextBox:true, margin:0, valign:"top",
        fontFace:HEAD, fontSize:19, bold:true, color:INK });
    body(s, x+0.3, y+1.1, cw-0.6, 2.0, c[1], SLATE, 12.5);
    s.addText(c[2], { x:x+0.3, y:y+2.98, w:cw-0.6, h:0.66, isTextBox:true, margin:0, valign:"top",
        fontFace:HEAD, fontSize:30, bold:true, color:NAVY });
    s.addText(c[3], { x:x+0.3, y:y+3.68, w:cw-0.6, h:0.4, isTextBox:true, margin:0, valign:"top",
        fontFace:BODY, fontSize:12, color:MID });
  });
  s.addNotes("Each of the three is backed by a measurement, and the power one is a ratio rather than a wattage.");
}

// ============ 9. CORRECTNESS ============
{
  const s = lightSlide("The device is never the first implementation", "CORRECTNESS");
  body(s, M, 1.78, CW, 0.8,
    "A NumPy reference of exactly the device arithmetic is checked against the trained float model. The real device C is then compiled for the host and checked against that reference. Only then does it reach the board.",
    SLATE, 15);
  const chain=[["Float model","PyTorch, 92.8 % test"],["NumPy reference","exact device arithmetic"],
               ["Device C on host","the same source as the board"],["Hardware","94.0 % core accuracy"]];
  const gap=0.32, bw=(CW-3*gap)/4, y=2.85, bh=1.4;
  chain.forEach((c,i)=>{
    const x=M+i*(bw+gap), last=(i===3);
    card(s, x, y, bw, bh, last?NAVY:LIGHT);
    s.addText(c[0], { x:x+0.25, y:y+0.3, w:bw-0.5, h:0.36, isTextBox:true, margin:0, valign:"top",
        fontFace:BODY, fontSize:14, bold:true, color:last?WHITE:INK });
    body(s, x+0.25, y+0.72, bw-0.5, 0.5, c[1], last?ICE:SLATE, 11.5);
    if(i<3) arrow(s, x+bw+0.04, y+bh/2-0.1, 0.24, 0.2);
  });
  s.addText("Agreement at every step, on the same inputs", { x:M, y:4.55, w:CW, h:0.34, isTextBox:true,
    margin:0, valign:"top", fontFace:BODY, fontSize:13, bold:true, color:INK });
  const tol=[["transform","1.4 × 10⁻⁵"],["MFCC frame","5.5 × 10⁻⁶"],["inference logits","5 × 10⁻⁸"]];
  const tw=3.78;
  tol.forEach((t,i)=>{
    const x=M+i*(tw+0.28);
    card(s, x, 4.98, tw, 1.15, LIGHT);
    s.addText(t[0], { x:x+0.28, y:5.16, w:tw-0.6, h:0.32, isTextBox:true, margin:0, valign:"top",
        fontFace:BODY, fontSize:12.5, color:SLATE });
    s.addText(t[1], { x:x+0.28, y:5.5, w:tw-0.6, h:0.45, isTextBox:true, margin:0, valign:"top",
        fontFace:HEAD, fontSize:19, bold:true, color:NAVY });
  });
  s.addText("A disagreement on hardware is therefore a hardware question, never an open one.",
    { x:M, y:6.42, w:CW, h:0.4, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:14, italic:true, color:SLATE });
  s.addNotes("This is why the board brought up cleanly: the maths was settled before any flash.");
}

// ============ 10. WHAT IS NOT CLAIMED ============
{
  const s = lightSlide("What is not claimed", "LIMITS");
  s.addText("Stating the limits precisely is part of the result.",
    { x:M, y:1.78, w:CW, h:0.4, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:15, color:SLATE });
  const lim=[["Two different accuracies","94.0 percent is core accuracy, scored on pre computed feature grids. End to end accuracy through capture, gate and features is 47 to 57 percent, and at this sample size the three configurations are not statistically separable.","a wider replay corpus and a longer live phase"],
             ["Power is a ratio","Idle residency, not a wattage. No ammeter was used. The firmware side is ready and the absolute current at the IDD jumper is still to be measured.","a DC ammeter in series at the IDD jumper"],
             ["TrustZone is cut","The program plan promised secure world weight isolation in section 6.6. It was cut as separable from the co-optimisation thesis, and the descope is documented rather than glossed over.","a secure world partition behind an NSC veneer"]];
  const cw=3.78, gap=0.28, y=2.4, ch=3.8;
  lim.forEach((l,i)=>{
    const x=M+i*(cw+gap);
    card(s, x, y, cw, ch, LIGHT);
    s.addText(l[0], { x:x+0.3, y:y+0.32, w:cw-0.6, h:0.75, isTextBox:true, margin:0, valign:"top",
        fontFace:HEAD, fontSize:17, bold:true, color:INK });
    body(s, x+0.3, y+1.18, cw-0.6, 1.55, l[1], SLATE, 12.5);
    s.addText("WHAT WOULD CLOSE IT", { x:x+0.3, y:y+2.85, w:cw-0.6, h:0.3, isTextBox:true,
        margin:0, valign:"top", fontFace:BODY, fontSize:10, bold:true, color:MID, charSpacing:1 });
    body(s, x+0.3, y+3.16, cw-0.6, 0.55, l[2], NAVY, 12.5);
  });
  s.addText("Every figure in this deck traces back to a timestamped run under experiments/ with its raw device capture alongside it.",
    { x:M, y:6.5, w:CW, h:0.4, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:13, italic:true, color:MID });
  s.addNotes("Intellectual honesty scores better than an overreaching claim.");
}

// ============ 11. CLOSE ============
{
  const s = darkSlide();
  s.addShape(pres.ShapeType.ellipse, { x:-1.8, y:4.2, w:4.6, h:4.6, fill:{color:"27306E"}, line:NOLINE() });
  s.addShape(pres.ShapeType.ellipse, { x:10.9, y:-1.2, w:4.4, h:4.4, fill:{color:"27306E"}, line:NOLINE() });
  s.addText("A template, not a one off", { x:M, y:1.45, w:9.6, h:0.8, isTextBox:true, margin:0,
      valign:"top", fontFace:HEAD, fontSize:40, bold:true, color:WHITE });
  s.addText("The signal source is one interface with two implementations, and the pipeline above it does not know where samples come from. The task graph and the adaptation primitives carry over to any sensor domain: vibration, audio, temperature, biosignals.",
    { x:M, y:2.5, w:8.4, h:1.5, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:16, color:ICE, lineSpacingMultiple:1.25 });
  const stats=[["98.2 ms","adaptive latency"],["1.79×","longer asleep"],["0","dropped blocks"]];
  stats.forEach((st,i)=>{
    const x=M+i*3.0;
    s.addText(st[0], { x, y:4.35, w:2.8, h:0.62, isTextBox:true, margin:0, valign:"top",
        fontFace:HEAD, fontSize:32, bold:true, color:AMBER });
    s.addText(st[1], { x, y:5.0, w:2.8, h:0.35, isTextBox:true, margin:0, valign:"top",
        fontFace:BODY, fontSize:13, color:ICE });
  });
  s.addText("github.com/kushal-script/utkernel-rtos-adaptive-kws",
    { x:M, y:5.95, w:CW, h:0.4, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:16, bold:true, color:WHITE });
  s.addText("MIT licensed   ·   builds and reproduces every number from the board alone, no external hardware",
    { x:M, y:6.42, w:CW, h:0.4, isTextBox:true, margin:0, valign:"top",
      fontFace:BODY, fontSize:12.5, color:MID });
  s.addNotes("Open source, reproducible from the board alone.");
}

pres.writeFile({ fileName: path.join(REPO, "docs/TRON2026_intro_slides.pptx") })
  .then(f => console.log("written:", f));
