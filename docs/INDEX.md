# GLIRPS Introduction and Overview

GLIRPS is a synth sound effect engine for games, built on the model of the MOS 6581/8580 sound chip (SID). These documents call that chip the *reference chip*, and reference-hardware values are the ones that reproduce its resolution. Nothing is recorded and no audio files ship. Every laser, thruster, and impact is synthesised while the game runs, from a short definition: a handful of voices with triangle, sawtooth, pulse, and pitched noise, an envelope on each, one shared resonant filter over the group, and tables of timed values that move those parameters, sampled fifty times a second by default. That update rate is most of why it sounds like a SID.

It is not a SID emulator. The engine underneath runs in floating point at full resolution and models none of the chip's defects. The crunch that gives it the period character is a separate stage applied on top, dialled by a single number from full hardware grit down to clean modern synthesis.

Three properties follow from the design and are worth stating first:

1. A sound is a small, self-contained definition, so a consumer can edit and audition it without restarting the game.
2. The same definition and the same seed produce the same result every time within one process.
3. Gains are multipliers against a fixed nominal level rather than fractions of full scale, so loudness is expressed against a known reference.

The name reads three ways: an onomatopoeic rendering of what a SID arpeggio sounds like, an acronym for *Gritty Little Instrument for Retro Procedural Sounds*, and a recursive description of what it produces, *Glirpsy Lo-fi Irritating Retro Pew-Pew Sounds*.

## What it does not do

GLIRPS only synthesises. The *consumer*, meaning whatever program embeds it, supplies everything else:

- **Devices and output.** It knows nothing about a sound card, a sample format negotiation, or a callback thread. It hands back samples.
- **Files.** It parses nothing. The consumer builds a sound program and hands it over.
- **Timing and scheduling.** It does not decide when a sound should play, does not read a clock, and owns no transport between threads. When and in what order the consumer applies the blocks it receives, and what to do about a late one, is also the consumer's policy.
- **Space.** Panning, distance attenuation, bus routing, and a master limiter live behind [the mixer stage](mixer.md), which the consumer may replace.

## Vocabulary

Terms used throughout the reference documents are defined once in [the glossary](glossary.md).

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
  sum the instance's voices ──► shared filter ──► output gain ──► decimate to the output rate
      ▼
  mixer: one mono block submitted per live instance
══════╪═══════════════════════════════════════════════════ consumer ═
      ▼
  output buffer ──► device
```

The figure supplements the description; the text and the linked documents are authoritative.

## The two sides

The entry points divide into a **control side** that decides what should be heard and a **render side** that produces samples. The two touch no shared mutable state, which is what lets a consumer run them on different threads without GLIRPS owning a lock or a queue.

| Stage | Side |
| --- | --- |
| Table evaluation | Control |
| Grit quantization | Control |
| Chip instance and voice pools, instance lifetime | Control |
| Oscillators, oversampling, decimation | Render |
| Envelopes, filter, per-voice and per-chip gain | Render |
| Parameter smoothing | Render |
| Mixing | Render |

The consumer decides which thread runs which side, and carries parameter blocks from one to the other. Which calls belong to which side, and which may overlap, is stated in the [API reference](../README.md#api-reference).

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
| Effective tick rate | base rate times `GritProfile::tick_multiplier`, which ranges 1 to 16. A sound's *effective tick* is one step of that faster grid; a *base tick* is one step of the 50 Hz grid |
| Voices per chip instance | 1 to 4 |
| Render granularity | 64 frames per sub-block, which is about 1.3 ms at 48 kHz |
| Oscillator oversampling | 16x by default, set with `synthConfigure`, decimated back down. Any positive integer; oscillators, envelopes, and filter run at that multiple of the output rate, so CPU cost grows with it |
| Nominal source level | -12 dBFS at a chip instance's output, with every gain at 1.0 |
| Pitch range | C-0 to B-9. A sound program addresses pitch as a note plus a cents offset, never as a frequency; the block renderer takes hertz |

`SoundProgram::output_gain` and every `VoiceProgram::gain` is a **linear multiplier relative to the nominal source level**, not a fraction of full scale.

## Determinism

Every random value comes from a generator seeded by the seed given when the sound starts. Nothing reads a clock, an address, or an entropy source. Two renders of the same program with the same seed, in one process, are byte-identical.

No guarantee is made across machines or compilers.

## Grit

The synthesis engine itself is character-neutral and runs at continuous resolution. All lo-fi character comes from one stage: after an effective tick is evaluated and before its parameter block is handed on, every numeric value is snapped onto a lattice whose step size comes from the grit profile's bit depths.

A single **grit scale**, a property of the synthesiser rather than of any one sound, interpolates that step size between full float resolution at 0 and the profile's own bit depths at 1. The consumer sets it with `synthSetGritScale`; values outside 0 to 1 are clamped. The default is 1, so every sound plays with its own profile. Setting the profile to reference-hardware values gives the reference character; relaxing them gives clean modern synthesis with no engine change.

One exception is deliberate: noise is generated by a shift register and is digital by nature rather than by quantization, so it stays digital at every grit setting.

## Real-time behaviour

Rendering is safe to run on a real-time audio thread. `synthRender` does not allocate memory, take a lock, make a system call, or log. `synthStart` does not allocate either. Allocation happens only when objects are created or destroyed and, when the oversampling factor is raised above its default, in `synthConfigure`. A consumer mixer must obey the same rules (see [the mixer stage](mixer.md)).

## Environment: allocation and logging

Allocation and logging belong to the consumer too. Both are optional and C-style: a function pointer plus a `void*` context, like the mixer callbacks.

- **Allocation.** `AllocatorCallbacks` replaces the default C++ allocation for every heap allocation of a synth or block renderer, including its standard containers. Without it, the default allocation is used.
- **Logging.** `LogCallbacks` receives a severity (`debug`, `warning`, `error`) and a zero-terminated message with its length. The minimum severity is `warning` by default. A message below it is never formatted. Without a log callback, GLIRPS is silent.
- **Scope.** Both are part of an `Environment` passed to `synthCreate` or `blockRendererCreate`. Each object keeps its own copy, so there is no global setting and two objects MAY use different environments.
- **Threading.** Any GLIRPS function that takes the handle of an object, and the create function itself, MAY call the object's log callback. The callbacks run only during a call of a GLIRPS function, on the thread that made the call, never on another thread. GLIRPS does not synchronize them; the consumer provides any synchronization they need. The callbacks and their contexts stay valid until the matching destroy call returns.

The declarations and their full rules are in `include/glirps/Environment.hpp`.

## Headers

All public headers are under `include/glirps/` and are included as `<glirps/...>`.

| Header | Contents |
| --- | --- |
| `glirps.hpp` | The synth API and the mixer callbacks. It includes the sound program, parameter block, control parameter, environment, and constants headers |
| `glirps_render.hpp` | The block renderer API |
| `glirps_handles.hpp` | Optional. `SynthOwner` and `BlockRendererOwner`, RAII owners that destroy the handle on scope exit. The other headers do not include it |

## Document set

- [Glossary](glossary.md): the terms the reference documents define, each with a stable anchor to link to.
- [The sound program](sound-program.md): the data structure that carries one sound into the library, and the unit and range of every field in it.
- [Chip instance manual](chip-instance.md): what a playing sound holds, what every parameter does to it, and the states it moves through.
- [Parameter blocks](parameter-blocks.md): what one effective tick produces, and how it is produced.
- [The block renderer](block-renderer.md): rendering one chip's worth of voices directly, without tables, pools, or a mixer.
- [The mixer stage](mixer.md): how rendered instances reach the output buffer, and what a consumer mixer is responsible for.
