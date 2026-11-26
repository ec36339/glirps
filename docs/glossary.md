# GLIRPS Glossary

Terms as GLIRPS uses them. Each entry has a stable anchor of the form `glossary.md#<term>`, the lowercase term with hyphens for spaces. Entries link to the section that carries the detail.

- <a id="accumulate-table"></a>*Accumulate table*: a table whose value is added to a running total once per [effective tick](#effective-tick) instead of replacing the parameter: `pitch_slide` and `pulse_width_delta`. See [table values](sound-program.md#table-values).
- <a id="base-tick"></a>*Base tick*: one step of the base control grid. See [fixed quantities](INDEX.md#fixed-quantities).
- <a id="block-renderer"></a>*Block renderer*: renders one chip's worth of voices, driven by [parameter blocks](#parameter-block) or direct setters, with no tables, pools, or mixer. See [the block renderer](block-renderer.md) and [synth and block renderer](INDEX.md#synth-and-block-renderer).
- <a id="chip-instance"></a>*Chip instance*: a small fixed group of voices sharing **one filter and one output gain**. One playing sound is one chip instance. See [the chip instance manual](chip-instance.md).
- <a id="consumer"></a>*Consumer*: the program that embeds the library. It owns files, devices, timing, and threads. See [what GLIRPS does not do](INDEX.md#what-it-does-not-do).
- <a id="control-side"></a>*Control side*: the entry points that decide what should be heard: table evaluation, grit quantization, and the pools and lifetime of instances. The counterpart of the [render side](#render-side). See [the two sides](INDEX.md#the-two-sides).
- <a id="effective-tick"></a>*Effective tick*: one step of a sound's own control grid, on which its [tables](#table) are sampled. See [blocks per base tick](parameter-blocks.md#blocks-per-base-tick) and [fixed quantities](INDEX.md#fixed-quantities).
- <a id="gate"></a>*Gate*: a voice's on/off state: on starts and holds its envelope, off releases it. See [gain and the envelope](chip-instance.md#gain-and-the-envelope) and [gate program](sound-program.md#gateprogram).
- <a id="grit"></a>*Grit*: the lo-fi character that quantization gives a sound. See [grit](INDEX.md#the-grit-stage) and [the grit lattice](parameter-blocks.md#the-grit-lattice).
- <a id="grit-profile"></a>*Grit profile*: the quantization parameter set applied at each [effective tick](#effective-tick): tick rate multiplier, per-parameter bit depths, and stepping behavior. See [grit profile](sound-program.md#gritprofile).
- <a id="grit-scale"></a>*Grit scale*: a property of the synth, not of one sound, that interpolates the quantization step between full float resolution at 0 and the [grit profile](#grit-profile) bit depths at 1. See [grit](INDEX.md#the-grit-stage).
- <a id="handle"></a>*Handle*: an opaque value naming a synth, a block renderer, or one chip instance. See [synth and block renderer](INDEX.md#synth-and-block-renderer).
- <a id="instance-pool"></a>*Instance pool*: the fixed set of [chip instance](#chip-instance) slots, together with the *voice pool* of voice slots. Both sizes are given to `synthCreate`. Starting a sound takes one instance slot and one voice slot per voice, and fails if either is short. See [lifetime states](chip-instance.md#lifetime-states).
- <a id="instance-time"></a>*Instance time*: time since a chip instance started, in [subticks](#subtick). Table times are measured from it. See [time](sound-program.md#time).
- <a id="mixer"></a>*Mixer*: the stage that combines the mono samples of all live instances into the output buffer. Replaceable by the consumer. See [the mixer stage](mixer.md).
- <a id="nominal-source-level"></a>*Nominal source level*: the reference level that gains are relative to. See [output gain and level](chip-instance.md#output-gain-and-level).
- <a id="one-shot-sound"></a>*One-shot sound*: a sound whose [sound program](#sound-program) names a release time, so it closes its own [gate](#gate). See [gate program](sound-program.md#gateprogram).
- <a id="onset-delay"></a>*Onset delay*: a number of base ticks, given when an instance starts, that withholds its onset so it lands on the base grid. It is not part of the [sound program](#sound-program). See [time](sound-program.md#time).
- <a id="parameter-block"></a>*Parameter block*: everything one chip instance sounds like at one instant. See [parameter blocks](parameter-blocks.md).
- <a id="pass"></a>*Pass*: one run through a [table](#table), from its start or loop point to its end. See [table evaluation](sound-program.md#table-evaluation).
- <a id="reference-chip"></a>*Reference chip*: the MOS 6581/8580 (SID) that the synthesis model follows. "Reference-hardware values" are the quantization settings that reproduce its resolution. See [the overview](INDEX.md).
- <a id="render-side"></a>*Render side*: the entry points that produce samples: oscillators, envelopes, filter, gain, smoothing, and mixing. The counterpart of the [control side](#control-side). See [the two sides](INDEX.md#the-two-sides).
- <a id="sound-program"></a>*Sound program*: one sound in the form the library consumes, the type `SoundProgram`, built by the [consumer](#consumer). See [the sound program](sound-program.md).
- <a id="sub-block"></a>*Sub-block*: the fixed render granularity of 64 frames. A parameter block is applied at a sub-block boundary. See [applying a block](parameter-blocks.md#applying-a-block).
- <a id="subtick"></a>*Subtick*: 1/16 of a [base tick](#base-tick), the unit of every time in a [sound program](#sound-program). See [time](sound-program.md#time).
- <a id="sustained-sound"></a>*Sustained sound*: a sound whose program names no release time. The [consumer](#consumer) holds and releases its [gate](#gate) with `synthSetGate`. See [gate program](sound-program.md#gateprogram).
- <a id="synth"></a>*Synth*: the top-level object that runs whole sounds. See [synth and block renderer](INDEX.md#synth-and-block-renderer).
- <a id="table"></a>*Table*: a list of timed segments with an optional loop point, driving one parameter. See [`Table<Row>`](sound-program.md#tablerow).
