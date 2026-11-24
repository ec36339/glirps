# Parameter Blocks

A *parameter block* is everything one [chip instance](chip-instance.md) sounds like at one instant. It is what the library evaluates a [sound program](sound-program.md) into, one effective tick at a time. It is the only thing that crosses from the side that decides what should be heard to the side that produces samples.

Everything else about a sound is either fixed when the instance starts, such as `EnvelopeSpec` and `FilterProgram::saturation`, or lives entirely on the [control side](glossary.md#control-side), such as table positions and accumulators. A block is a flat set of already-resolved numbers, small enough to copy and with no pointers into anything.

Two properties follow, and both are the reason the design has this shape:

1. The [render side](glossary.md#render-side) needs **no access to the `SoundProgram`**, so the two sides share no mutable state and can run on different threads without a lock.
2. A block can be **carried across any transport** the consumer likes: a queue, a ring, a direct call.

## Contents

| Value | Unit | Range | Meaning |
| --- | --- | --- | --- |
| Target instance | none | any handle | The chip instance this block applies to. An expired handle is ignored rather than misapplied |
| Filter modes | none | any combination of the four responses | Responses of the shared filter, summed. All four unset passes the input through |
| Cutoff | hertz | 1 to just under half the sample rate | Shared filter cutoff. The upper bound is not an audibility limit |
| Resonance | dimensionless Q | 0.5 to 20 | Shared filter resonance. The top of the range is where the filter approaches self-oscillation |
| Output gain | linear multiplier | 0 to `gain_ceiling` | Shared output gain, relative to the nominal source level. Authored values lie in 0 to 1; only the external `gain_multiplier` can take it above 1 |
| Voice count | count | 1 to 4 | How many of the voice entries are meaningful. A block the library emits always carries at least one |
| Voice waveform | none | one of the four waveforms | What this voice emits this tick |
| Voice frequency | hertz | 0 to half the sample rate | Oscillator frequency. Derived, never authored. Zero holds the phase |
| Voice pulse width | normalised duty | 0 to 1 | Duty cycle, where 0.5 is square. Read only under the pulse waveform, and 0 and 1 are both a constant level |
| Voice gain | linear multiplier | 0 to `gain_ceiling` | Mix gain applied after the envelope, on the same scale as the output gain. Authored values lie in 0 to 1 |
| Voice gate | none | on or off | On starts or holds attack, decay, and sustain. Off starts release |

The ranges are what the library guarantees for every block it emits. For a block the consumer builds itself, for the block renderer or `synthApplyBlock`, they are preconditions: applying a block does not clamp, and a voice count above 4 reads past the voice array.

Every value the block does not carry is fixed for the instance's lifetime. That includes envelope times and sustain level, filter saturation, ring modulation and hard sync routing, the seed, and every table.

Authored gains stop at 1.0, which is the nominal source level. A block can carry a larger gain only through the external `gain_multiplier`, up to `gain_ceiling`, which is 1000 and exists only to stop a runaway multiplier. Level management beyond that belongs downstream in [the mixer stage](mixer.md).

## How one is produced

Per instance, per effective tick, in this order:

1. **Evaluate every table** at the block's time: instance time minus the voice's `start_subtick` for a voice table, instance time itself for a filter table, wrapped at the table's own loop point. Each segment steps or interpolates between its values. A random value is drawn at the first block of a [pass](glossary.md#pass) that uses it and kept for the rest of that pass.
2. **Accumulate** `VoiceTables::pitch_slide`, in cents, and `VoiceTables::pulse_width_delta`, in duty, adding each one's current value once per effective tick. Both carry across ticks and are clamped by `pitch_slide_bounds` and `pulse_width_delta_bounds` where present.
3. **Assemble each voice's frequency** from `SoundProgram::base_pitch`, the pitch variation seeded against `SoundProgram::scale`, the current `VoiceTables::arpeggio` and `VoiceTables::pitch_offset` offsets, the accumulated slide, and any external `pitch_cents`.
4. **Resolve the gate**: the instance's own gate state, derived from `GateProgram::release_subtick` or held by the consumer, ANDed with the voice's `VoiceTables::gate` value when it has one.
5. **Quantize** every numeric value onto the grit lattice.
6. **Clamp** every value to the ranges in the table above.

Steps 5 and 6 are in that order on purpose: clamping last means quantization cannot round a value past a bound, so a block never carries an out-of-range value regardless of what the program held. A program with a value outside its stated range produces a wrong sound, never a crash.

The clamp happens exactly once, here. Applying a block does not clamp again, so the value that reaches a voice is the value the control side produced.

## Blocks per base tick

One base tick of the 50 Hz grid produces **as many blocks as `GritProfile::tick_multiplier`**, one per effective tick. A sound at multiplier 4 evaluates its tables at four evenly spaced times and emits four blocks for every 20 ms of the base grid. The block with zero-based index `n` sits at instance time `n * 16 / tick_multiplier` subticks, where a subtick is 1/16 of a base tick. The multiplier changes how finely tables are sampled, not when their values fall; the accumulate tables are the exception, since they add their value once per effective tick.

Spacing those blocks across that 20 ms is the consumer's job. Tick evaluation can optionally report, per emitted block, the sub-block boundary its own tick starts at and the boundary the next tick starts at, which is the sanctioned way to derive a render span from a tick index. A consumer driving a real device usually ignores that and derives block timing from the device sample clock instead.

## The grit lattice

Every numeric value in a block sits on a lattice, not on the continuous range its bounds describe. The ranges say what values are legal; the lattice says which of them can actually occur.

The step size comes from `GritProfile::bit_depths`, scaled by a synthesiser-wide grit scale that interpolates between full float resolution at 0 and the profile's own depths at 1. Two details are worth knowing when reading a captured stream:

- **Pitch and cutoff quantize against a fixed reference span**, not against the field's full digital range. Cutoff steps against roughly 30 Hz to 12 kHz, which is the analog span of the reference chip's filter core, so an 11-bit cutoff steps by about 6 Hz.
- **`VoiceProgram::gain` does not use `BitDepths::gain`.** That depth models the reference chip's 4-bit master volume register and applies to `SoundProgram::output_gain` only. Per-voice level comes from the chip's 8-bit envelope stage and quantizes at a fixed 8-bit step.

## Applying a block

A block replaces the instance's parameters wholesale. What happens to each value on arrival differs:

| Value | On arrival |
| --- | --- |
| Waveform, frequency, pulse width, gate | Immediate step, no smoothing |
| Voice gain, cutoff, resonance, output gain | Ramped to the new value over exactly one sub-block, about 1.3 ms |

A block is applied at a sub-block boundary, and by the next boundary the ramp is done.
