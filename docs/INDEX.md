# GLIRPS Introduction and Overview

GLIRPS is a synth sound effect engine for games, built on the model of the MOS 6581/8580 sound chip (SID). These documents call that chip the *reference chip*, and reference-hardware values are the ones that reproduce its resolution. Nothing is recorded and no audio files ship. Every laser, thruster, and impact is synthesized while the game runs, from a short definition: a handful of voices with triangle, sawtooth, pulse, and pitched noise, an envelope on each, one shared resonant filter over the group, and tables of timed values that move those parameters, sampled fifty times a second by default.

It is not a SID emulator. The engine underneath runs in floating point at full resolution and models none of the chip's defects. The crunch that gives it the period character is a separate stage applied on top, dialed by a single number from full hardware grit down to clean modern synthesis.

GLIRPS, Retro sound effect synthesizer for games inspired by the 6581/8580 SID.

The name mimics the sound of a SID arpeggio. It also stands for *Gritty Lo-fi Instrument for Retro Procedural Sounds*, or alternatively *GLIRPS Literally Isn't Reproducing any Particular SID*.

## What it does not do

GLIRPS only synthesizes. The *consumer*, meaning whatever program embeds it, supplies everything else:

- **Devices and output.** It knows nothing about a sound card, a sample format negotiation, or a callback thread. It hands back samples.
- **Files.** It parses nothing. The consumer builds a sound program and hands it over.
- **Timing and scheduling.** It does not decide when a sound should play, does not read a clock, and owns no transport between threads. The consumer also decides when and in what order to apply blocks, including late ones.
- **Space.** Panning, distance attenuation, bus routing, and a master limiter live behind [the mixer stage](mixer.md), which the consumer may replace.

## The path a sound takes

```
CONSUMER
  authored form (consumer-defined)  ──►  loader  ──►  SoundProgram
                                             base pitch, voice list, envelopes,
                                             filter, grit profile, tables
      │
      │  start the sound, with a seed
══════╪═══════════════════════════════════════════════════ GLIRPS ═══
      ▼
  chip instance pool ──► handle to the playing instance
      │
  CONTROL SIDE, once per instance per effective tick
      ▼
  evaluate every table at the block's time
      ▼
  assemble pitch, pulse width, gain, gate, cutoff, resonance
      ▼
  grit stage: quantize every value onto the profile's lattice
      ▼
  clamp to range ──► parameter block, one per effective tick
══════╪═══════════════════════════════════════════════════ consumer ═
      │  transport, scheduling, threading
══════╪═══════════════════════════════════════════════════ GLIRPS ═══
  RENDER SIDE, once per sub-block of 64 frames
      ▼
  apply the block to its chip instance
      ▼
  per voice: oscillator, oversampled ──► envelope ──► voice gain
             noise register clocked by the oscillator
             optional ring modulation or hard sync against another voice
      ▼
  sum the instance's voices ──► shared filter ──► decimate to the output rate ──► output gain
      ▼
  mixer: mono samples submitted per live instance
══════╪═══════════════════════════════════════════════════ consumer ═
      ▼
  output buffer ──► device
```

## The two sides

The entry points divide into a **control side** that decides what should be heard and a **render side** that produces samples. A consumer can run them on different threads; GLIRPS owns no lock or queue.

| Stage | Side |
| --- | --- |
| Table evaluation | Control |
| Grit quantization | Control |
| Chip instance and voice pools, instance lifetime | Control |
| Oscillators, oversampling, decimation | Render |
| Envelopes, filter, per-voice and per-chip gain | Render |
| Parameter smoothing | Render |
| Mixing | Render |

The consumer decides which thread runs which side, and carries parameter blocks from one to the other. Which calls belong to which side, and which may overlap, is stated in the [threading reference](https://ec36339.github.io/glirps/threading.html).

## Synth and block renderer

The library has two top-level objects, each created with an optional [environment](#environment-allocation-and-logging):

- A **synth** runs whole sounds. Starting a sound returns a handle to a [chip instance](chip-instance.md). The synth evaluates that instance's tables on the control side and renders it on the render side, and it mixes every live instance through [the mixer stage](mixer.md).
- A **block renderer** renders one chip's worth of voices and nothing else. The consumer drives it with [parameter blocks](parameter-blocks.md) or with direct setters for voices, envelopes, filter, and output gain, and receives samples. It has no tables, no pools, and no mixer. See [the block renderer](block-renderer.md).

A **handle** is an opaque value naming one object or one instance. A handle to an instance becomes *expired* when the instance is released or the synth is reconfigured. Passing an expired handle to any function is safe and has no effect.

## Fixed quantities

| Quantity | Value |
| --- | --- |
| Control grid base rate | 50 Hz, so one base tick is 20 ms |
| Subtick | 1/16 of a base tick, the time unit of a sound program |
| Effective tick rate | base rate times `GritProfile::tick_multiplier` |
| Voices per chip instance | 1 to 4 |
| Render granularity | 64 frames per sub-block, which is about 1.3 ms at 48 kHz |
| Oscillator oversampling | set with `synthConfigure` |
| Pitch range | C-0 to B-9 |

## Determinism

Every random value comes from a generator seeded by the seed given when the sound starts. Nothing reads a clock, an address, or an entropy source. Two renders of the same program with the same seed, in one process, are byte-identical.

No guarantee is made across machines or compilers.

## The grit stage

The synthesis engine itself is character-neutral and runs at continuous resolution. All lo-fi character comes from one stage: after an effective tick is evaluated and before its parameter block is handed on, every numeric value is snapped onto a lattice whose step size comes from the grit profile's bit depths.

A single **grit scale**, a property of the synth rather than of any one sound, interpolates that step size between full float resolution at 0 and the profile's own bit depths at 1. The consumer sets it with `synthSetGritScale`; values outside 0 to 1 are clamped. The default is 1, so every sound plays with its own profile. Setting the profile to reference-hardware values gives the reference character; relaxing them gives clean modern synthesis with no engine change.

Noise stays digital at every grit scale.

## Real-time behavior

Rendering is safe to run on a real-time audio thread. `synthRender` does not allocate memory, take a lock, make a system call, or log. `synthStart` does not allocate either. Allocation happens only when objects are created or destroyed, and when `synthConfigure` or `blockRendererReconfigure` raises the oversampling factor above the one the object was created with (for a synth, the default). A consumer mixer must obey the same rules (see [the mixer stage](mixer.md)).

## Environment: allocation and logging

Allocation and logging belong to the consumer too. Both are optional and C-style: a function pointer plus a `void*` context, like the mixer callbacks.

- **Allocation.** `AllocatorCallbacks` replaces the default C++ allocation for every heap allocation of a synth or block renderer, including its standard containers. Without it, the default allocation is used.
- **Logging.** `LogCallbacks` receives a severity (`debug`, `warning`, `error`) and a zero-terminated message with its length. The minimum severity is `warning` by default. A message below it is never formatted. Without a log callback, GLIRPS is silent.
- **Scope.** Both are part of an `Environment` passed to `synthCreate` or `blockRendererCreate`. Each object keeps its own copy, so there is no global setting and two objects may use different environments.
- **Threading.** Any GLIRPS function except `synthRender` that takes the handle of an object, and the create function itself, may call the object's log callback. The callbacks run only during a call of a GLIRPS function, on the thread that made the call, never on another thread. GLIRPS does not synchronize them; the consumer provides any synchronization they need. The callbacks and their contexts stay valid until the matching destroy call returns.

The declarations and their full rules are in `include/glirps/Environment.hpp`.

## Headers

All public headers are under `include/glirps/` and are included as `<glirps/...>`.

The GLIRPS API uses `glirps::shims::Span` and `glirps::shims::Array` in place of `std::span` and `std::array`.

| Header | Contents |
| --- | --- |
| `glirps.hpp` | The synth API and the mixer callbacks. It includes the sound program, parameter block, control parameter, environment, and constants headers |
| `glirps_render.hpp` | The block renderer API |
| `glirps_handles.hpp` | Optional. `SynthOwner` and `BlockRendererOwner`, RAII owners that destroy the handle on scope exit. The other headers do not include it |

## Document set

- [Tutorial](tutorial.md): a first program, from a sound program to samples, built step by step.
- [Glossary](glossary.md): the terms the reference documents define, each with a stable anchor to link to.
- [The sound program](sound-program.md): the data structure that carries one sound into the library, and the unit and range of every field in it.
- [Chip instance manual](chip-instance.md): what a playing sound holds, what every parameter does to it, and the states it moves through.
- [Parameter blocks](parameter-blocks.md): what one effective tick produces, and how it is produced.
- [The block renderer](block-renderer.md): rendering one chip's worth of voices directly, without tables, pools, or a mixer.
- [The mixer stage](mixer.md): how rendered instances reach the output buffer, and what a consumer mixer is responsible for.
