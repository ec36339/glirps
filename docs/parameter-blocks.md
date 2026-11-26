# Parameter Blocks

A *parameter block* is everything one [chip instance](chip-instance.md) sounds like at one instant. It is what the library evaluates a [sound program](sound-program.md) into, one effective tick at a time. It is the only thing that crosses from the side that decides what should be heard to the side that produces samples.

Everything else about a sound is either fixed when the instance starts, such as `EnvelopeSpec` and `FilterProgram::saturation`, or lives entirely on the [control side](glossary.md#control-side), such as table positions and accumulators. A block is a flat set of already-resolved numbers, small enough to copy and with no pointers into anything.

As a result:

1. The [render side](glossary.md#render-side) needs **no access to the `SoundProgram`**.
2. A block can be **carried across any transport** the consumer likes: a queue, a ring, a direct call.

## Contents

| Value | Unit | Range | Meaning |
| --- | --- | --- | --- |
| Target instance | none | any handle | The chip instance this block applies to. An expired handle is ignored |
| Filter modes | none | any combination of the four responses | Responses of the shared filter, summed. All four unset passes the input through |
| Cutoff | hertz | 1 to just under half the sample rate | Shared filter cutoff. The upper bound is not an audibility limit |
| Resonance | dimensionless Q | 0.5 to 20 | Shared filter resonance. The top of the range is where the filter approaches self-oscillation |
| Output gain | linear multiplier | 0 to `gain_ceiling` | Shared output gain, relative to the nominal source level. Authored values lie in 0 to 1 |
| Voice count | count | 1 to 4 | How many of the voice entries are meaningful. A block the library emits always carries at least one |
| Voice waveform | none | one of the four waveforms | What this voice emits this tick |
| Voice frequency | hertz | 0 to half the sample rate | Oscillator frequency. Derived, never authored. Zero holds the phase |
| Voice pulse width | normalized duty | 0 to 1 | Duty cycle. See [pulse width](chip-instance.md#pulse-width) |
| Voice gain | linear multiplier | 0 to `gain_ceiling` | Mix gain applied after the envelope, on the same scale as the output gain. Authored values lie in 0 to 1 |
| Voice gate | none | on or off | On starts or holds attack, decay, and sustain. Off starts release |

The ranges are what the library guarantees for every block it emits. For a block the consumer builds itself, for the block renderer or `synthApplyBlock`, they are preconditions: applying a block does not clamp, and a voice count above 4 reads past the voice array.

Level management belongs downstream in [the mixer stage](mixer.md).

## How one is produced

Per instance, per effective tick, in this order:

1. **Evaluate every table** at its [table time](sound-program.md#time) for the block. Each segment steps or interpolates between its values. A random value is drawn at the first block of a [pass](glossary.md#pass) that uses it and kept for the rest of that pass.
2. **Accumulate** `VoiceTables::pitch_slide`, in cents, and `VoiceTables::pulse_width_delta`, in duty, adding each one's current value once per effective tick. Both carry across ticks and are clamped by `pitch_slide_bounds` and `pulse_width_delta_bounds` where present.
3. **Assemble each voice's frequency** from `SoundProgram::base_pitch`, the pitch variation seeded against `SoundProgram::scale`, the current `VoiceTables::arpeggio` and `VoiceTables::pitch_offset` offsets, the accumulated slide, and any external `pitch_cents`.
4. **Resolve the gate**: the instance's own gate state, derived from `GateProgram::release_subtick` or held by the consumer, ANDed with the voice's `VoiceTables::gate` value when it has one.
5. **Apply [external controls](chip-instance.md#external-per-tick-control)** to the shared fields: the cutoff override and the gain multiplier.
6. **Quantize** every numeric value onto the grit lattice.
7. **Clamp** every value to the ranges in the table above.

Clamping comes after quantization, so quantization cannot round a value past a bound. A block never carries an out-of-range value regardless of what the program held. A program with a value outside its stated range produces a wrong sound, never a crash.

## Blocks per base tick

One base tick of the 50 Hz grid produces **as many blocks as `GritProfile::tick_multiplier`**, one per effective tick. A sound at multiplier 4 evaluates its tables at four evenly spaced times and emits four blocks for every 20 ms of the base grid. [Time](sound-program.md#time) says when each block sits.

Spacing those blocks across that 20 ms is the consumer's job. `synthEvaluateTick` can also return a `TickSubBlockSpan` per block: the sub-blocks containing the start of its tick and of the next one, counted from the instance's first tick. A consumer driving a real device usually ignores that and derives block timing from the device sample clock instead.

## The grit lattice

Every numeric value in a block sits on a lattice, not on the continuous range its bounds describe. The ranges say what values are legal; the lattice says which of them can actually occur.

The step size comes from `GritProfile::bit_depths`, scaled by a synth-wide grit scale that interpolates between full float resolution at 0 and the profile's own depths at 1. Two details are worth knowing when reading a captured stream:

- **Pitch and cutoff quantize against a fixed reference span**, not against the field's full digital range. Cutoff steps against roughly 30 Hz to 12 kHz, which is the analog span of the reference chip's filter core, so an 11-bit cutoff steps by about 6 Hz.
- **`BitDepths::gain` applies to `SoundProgram::output_gain` only**; voice gain quantizes at a resolution the profile does not set.

## Applying a block

A block replaces the instance's parameters wholesale. What happens to each value on arrival differs:

| Value | On arrival |
| --- | --- |
| Waveform, frequency, pulse width, filter modes, gate | Immediate step, no smoothing |
| Voice gain, cutoff, resonance, output gain | Ramped to the new value over exactly one sub-block, about 1.3 ms |

A block is applied at a sub-block boundary, and by the next boundary the ramp is done.
