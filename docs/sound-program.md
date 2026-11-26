# The Sound Program

A *sound program* is one sound in the form the library consumes. It is a plain aggregate with no behavior: base pitch, a voice list with per-voice envelopes and waveforms, a filter, a grit profile, a gate, and the tables of timed segments that make each parameter move.

The *consumer*, meaning whatever program embeds the library, builds one from whatever authored representation it uses.
A sound program is the input to starting a [chip instance](chip-instance.md). From then on the instance is driven by [parameter blocks](parameter-blocks.md), which the library evaluates from the program one effective tick at a time.

## Types

A sound program is built from plain aggregates in namespace `glirps`. None has a constructor, an invariant, or a method. The types are declared in these public headers, which carry the exact declarations and defaults:

| Header | Types |
| --- | --- |
| `SoundProgram.hpp` | `SoundProgram`, `Pitch`, `VoiceProgram`, `VoiceTables`, `EnvelopeSpec`, `FilterProgram`, `FilterTables`, `GateProgram` |
| `Table.hpp` | `Table<Row>`, `Segment`, `ValueRange<Row>`, `TableValue<Row>`, `InterpolationMode`, `AccumulateBounds` |
| `GritProfile.hpp` | `GritProfile`, `BitDepths`, `SteppingMode` |
| `FilterMode.hpp` | `FilterModeSet` |
| `Waveform.hpp` | `Waveform`: `triangle`, `sawtooth`, `pulse`, `noise`. See [waveform](chip-instance.md#waveform) |

## Field semantics

The ranges below are what the library accepts. A consumer's authored format may narrow any of them, and usually does, but may not widen one.

### Time

Every time in a sound program is in *subticks*: one subtick is `1 / subticks_per_base_tick`, that is 1/16, of a base tick.

- **Instance time.** The parameter block with zero-based index `n`, counting every block emitted for the instance from the first, sits at instance time `n * 16 / tick_multiplier` subticks. Comparisons against program times are exact integer arithmetic, so a multiplier that is not a power of two is evaluated at its exact fractional time.
- **Table time.** A `VoiceTables` table's time 0 is the later of its voice's `start_subtick` and the [onset](#time) set by the onset delay. The voice activates at the first block at or after that time. A `FilterTables` table runs on instance time directly.
- **Independence from the tick rate.** Table timing does not depend on `tick_multiplier`, which sets only how often tables are sampled. The exception is the accumulate tables, `pitch_slide` and `pulse_width_delta`, which add their current value once per effective tick.
- **Onset delay.** The onset delay given when an instance starts is not part of the program and stays in base ticks, so onset lands on the base grid. It counts from 1: a delay of 0 gates the voices at start, and a delay of N ≥ 1 gates them on the first block of the Nth base tick. A voice with both an onset delay and a `start_subtick` activates at the later of the two.

### `SoundProgram`

| Field | Unit | Range | Meaning |
| --- | --- | --- | --- |
| `grit` | `GritProfile` | see below | Quantization applied to every emitted parameter block |
| `base_pitch` | `Pitch` | see below | Pitch every `arpeggio` offset, `pitch_offset` value, and `pitch_slide` accumulation is relative to |
| `scale` | semitone degrees | each `[0, 11]`, ascending, at most 12 entries | Degrees for [seeded pitch variation](chip-instance.md#frequency). Empty (`scale_count` 0) applies none |
| `gate` | `GateProgram` | see below | Gate behavior |
| `filter` | `FilterProgram` | see below | The filter shared by every voice |
| `output_gain` | linear multiplier | `[0, 1]` | Base shared output gain, relative to `nominal_source_level_db` |
| `voices` | list | 1 to `max_voices_per_instance` entries | The voices this sound plays |

In these tables, a list field is a non-owning pointer plus its count (`scale` and `scale_count`, `voices` and `voice_count`), and "empty" means the count is 0. An optional field is a value plus a `has_` presence flag, and "absent" means that flag is false.

### `Pitch`

| Field | Unit | Range | Meaning |
| --- | --- | --- | --- |
| `semitone` | semitones above C-0 | `[0, 119]` | C-0 through B-9. A-4 is 57 and sounds at 440 Hz |
| `cents` | cents | `[-1200, 1200]` | Offset from `semitone`, one octave either way |

### `GritProfile`

| Field | Unit | Range | Meaning |
| --- | --- | --- | --- |
| `tick_multiplier` | count | `[1, 16]` | Effective tick rate is `control_grid_base_rate_hz * tick_multiplier`, and also the number of parameter blocks one base tick emits |
| `stepping_mode` | none | `SteppingMode::truncate` or `round` | How a value is mapped onto its quantization step. `truncate` drops the low bits, as the reference hardware does. `round` snaps to the nearest step |
| `bit_depths.pitch` | bits | `[1, 24]` | Quantization resolution of the derived frequency |
| `bit_depths.pulse_width` | bits | `[1, 24]` | Quantization resolution of `pulse_width` |
| `bit_depths.cutoff` | bits | `[1, 24]` | Quantization resolution of `cutoff_hz` |
| `bit_depths.resonance` | bits | `[1, 24]` | Quantization resolution of `resonance_q` |
| `bit_depths.gain` | bits | `[1, 24]` | Resolution of `SoundProgram::output_gain` only. See [the grit lattice](parameter-blocks.md#the-grit-lattice) |

### `VoiceProgram`

| Field | Unit | Range | Meaning |
| --- | --- | --- | --- |
| `waveform` | none | one enumerator | Base waveform, used on any tick no `tables.waveform` value replaces |
| `pulse_width` | normalized duty | `[0, 1]` | Base duty cycle, where 0.5 is square. Read only under `Waveform::pulse` |
| `gain` | linear multiplier | `[0, 1]` | Base mix gain, applied after this voice's envelope |
| `envelope` | `EnvelopeSpec` | see below | This voice's own envelope generator |
| `tables` | `VoiceTables` | see below | Automation of this voice |
| `start_subtick` | subticks of instance time | `>= 0` | Time this voice becomes active at, and the time origin of its tables. `0` is immediate |
| `ring_mod_source` | voice index | absent (`has_ring_mod_source` false), or `[0, voice_count - 1]` excluding its own index | Voice this voice ring-modulates against |
| `hard_sync_source` | voice index | absent (`has_hard_sync_source` false), or `[0, voice_count - 1]` excluding its own index | Voice this voice hard-syncs to |

### `EnvelopeSpec`

| Field | Unit | Range | Meaning |
| --- | --- | --- | --- |
| `attack_seconds` | seconds | `[0.002, 8.0]` | Attack time |
| `decay_seconds` | seconds | `[0.006, 24.0]` | Decay time |
| `sustain_level` | linear | `[0, 1]` | Sustain level |
| `release_seconds` | seconds | `[0.006, 24.0]` | Release time |

See [gain and the envelope](chip-instance.md#gain-and-the-envelope).

### `FilterProgram`

| Field | Unit | Range | Meaning |
| --- | --- | --- | --- |
| `modes` | none | any combination of the four `FilterModeSet` members: `lowpass` (on by default), `bandpass`, `highpass`, `notch` | Responses of the shared filter, summed. All four false passes the input through |
| `cutoff_hz` | hertz | `[20, 20000]` | Base cutoff |
| `resonance_q` | dimensionless Q | `[0.5, 20]` | Base resonance |
| `saturation` | normalized | `[0, 1]` | Amount of [filter saturation](chip-instance.md#the-shared-filter) |
| `tables` | `FilterTables` | see below | Automation of the shared filter. Its time origin is instance time 0 |

### `GateProgram`

| Field | Unit | Range | Meaning |
| --- | --- | --- | --- |
| `release_subtick` | subticks of instance time | absent (`has_release_subtick` false), or `>= 1` | Present: the gate is on for every block whose time is below this value, then releases. Absent: the gate is held by the consumer |

`release_subtick` is not the length of the sound. It is when the release begins; the sound continues for the longest `release_seconds` among its voices after that.

### `Table<Row>`

| Field | Unit | Range | Meaning |
| --- | --- | --- | --- |
| `segments`, `segment_count` | see `Segment` | non-empty, strictly ascending `start_subtick`, each starting at or after the previous segment's end | The timed segments of this table |
| `values`, `value_count` | see `TableValue<Row>` | non-empty | Pool every segment indexes into |
| `loops` | boolean | none | `false`: the last value holds once the last segment ends. `true`: evaluation wraps to `loop_start_subtick` |
| `loop_start_subtick` | subticks of table time | below the table end; meaningful only when `loops` is true | Where evaluation wraps to |

### `Segment`

| Field | Unit | Range | Meaning |
| --- | --- | --- | --- |
| `start_subtick` | subticks of table time | `>= 0` | Time of the segment's first value |
| `first_value` | index into `Table::values` | `first_value + Segment::value_count <= Table::value_count` | First value of this segment |
| `value_count` | count | `[1, 65535]` | Number of values |
| `interval_subticks` | subticks | `[1, 65535]`. A `step` segment with more than one value holds each value for at least one effective tick: `interval_subticks * tick_multiplier >= subticks_per_base_tick` | Time between consecutive values |
| `interpolation_mode` | none | one enumerator allowed by [Table values](#table-values) | How the segment moves between values |

A segment's *duration* is `value_count * interval_subticks` for `step` and `(value_count - 1) * interval_subticks` for `linear` and `catmull_rom`, so a one-value segment of any mode sets a value without moving it. Its *end* is `start_subtick` plus its duration. The *table end* is the end of the last segment.

`ValueRange<Row>` holds `min` and `max` in the row's own unit, both finite, with `max >= min`. Equal bounds are a fixed value. Unequal bounds are a uniform draw, inclusive of both for an integral `Row`; for a floating-point `Row` the standard's uniform real distribution draws from `[min, max)`, so `max` itself is not guaranteed reachable.

`AccumulateBounds` holds `min` and `max` in the bounded table's row unit, each meaningful only when its `has_min` or `has_max` flag is true. A side whose flag is false stays at the field's own default bound.

### Table values

| Table | Value unit | Interpolation | Applied as |
| --- | --- | --- | --- |
| `VoiceTables::waveform` | `Waveform` | `step` | Replaces `VoiceProgram::waveform` |
| `VoiceTables::arpeggio` | semitones, any `std::int32_t` | `step` | Offset from `SoundProgram::base_pitch`, replacing the previous value's offset |
| `VoiceTables::pitch_slide` | cents per effective tick, any finite float | `step` | **Accumulates**: the current value is added once per effective tick, clamped by `pitch_slide_bounds` where authored |
| `VoiceTables::pitch_offset` | semitones, fractional, any finite float | any | Offset from `SoundProgram::base_pitch`, added to the arpeggio offset and the slide accumulator |
| `VoiceTables::pulse_width_delta` | duty per effective tick, any finite float | `step` | **Accumulates** once per effective tick, then clamps to `[0, 1]`, further narrowed by `pulse_width_delta_bounds` where authored |
| `VoiceTables::pulse_width` | normalized duty `[0, 1]` | any | Replaces `VoiceProgram::pulse_width`. Not allowed together with `pulse_width_delta` |
| `VoiceTables::gain` | linear multiplier `[0, 1]` | any | Replaces `VoiceProgram::gain` |
| `VoiceTables::gate` | boolean | `step` | ANDed with the instance gate. It can narrow the window that gate leaves open, never widen it |
| `FilterTables::cutoff` | hertz `[20, 20000]` | any | Replaces `FilterProgram::cutoff_hz` |
| `FilterTables::resonance` | dimensionless Q `[0.5, 20]` | any | Replaces `FilterProgram::resonance_q` |

`waveform` and `gate` values are stored as plain values and are never random. Every other table stores `ValueRange` values.

### Table evaluation

A table is evaluated at table time `t`, per [Time](#time), by these rules:

1. An **absent table** leaves its parameter at the base value for the whole sound. It is not equivalent to a table of zeros.
2. **Before the first segment starts**, on any pass, a present table also leaves its parameter at the base value. An accumulate table (`pitch_slide`, `pulse_width_delta`) adds nothing there instead, so on a later pass its accumulated value is kept, not reset.
3. **Inside a segment**, with `u = (t - start_subtick) / interval_subticks`, `k = floor(u)`, and `f = u - k`, where `v[i]` is the segment's value `i`, counting from 0:
   1. `step` yields value `k`.
   2. `linear` yields `v[k] + f * (v[k + 1] - v[k])`.
   3. `catmull_rom` yields the uniform Catmull-Rom spline through `v[k - 1]`, `v[k]`, `v[k + 1]`, and `v[k + 2]`. A neighbor outside the segment repeats the segment's own first or last value. Segments never read each other's values.
4. **Between segments**, from a segment's end until the next segment starts, the segment's last value holds.
5. **After the table end**, a table with `loops` false holds its last value. A table with `loops` true evaluates `t` at `loop_start_subtick + (t - loop_start_subtick) mod (end - loop_start_subtick)`. A loop start inside a segment is allowed, and the value may jump at the wrap.
6. A **pass** is one traversal of the table from its start, or after a wrap from `loop_start_subtick`, to its end. A **random value** is drawn once per pass: at the first block it contributes to in that pass. Every later block of the same pass that uses it, including as a Catmull-Rom neighbor, sees the same draw. Each new pass of a looping table redraws.
7. An interpolated value passes through the **same clamps** as a stepped one, so Catmull-Rom overshoot needs no further clamp. An interpolated value that is not finite gives the parameter its base value for that block.
8. Tables are **evaluated independently** and may differ in segment layout, length, and loop point. Nothing aligns them.
9. Table length **does not govern how long a sound lasts**. `GateProgram` does, and there is no sound-level loop or length concept.

## What a sound program does not carry

| Value | Why it is elsewhere |
| --- | --- |
| A seed | Given per chip instance when it starts, so two instances of one program can differ |
| An onset or scheduling decision | When a sound starts is the consumer's, and never a property of the sound |
| The grit scale | A property of the synth, not of any one program |
| A sample rate | Given to `synthConfigure` |
| A frequency in hertz | Derived per tick; see [frequency](chip-instance.md#frequency) |
| A position or a bus | The chip instance is the unit of placement and routing, behind [the mixer stage](mixer.md) |

## Validation and preconditions

`synthStart` fails with `StartError::invalid_program` for:

1. `voices` empty, or longer than `max_voices_per_instance`.
2. A present `Table` with:
   - no segments or no values;
   - a segment with no values, a zero `interval_subticks`, or values outside `values`;
   - segment starts not strictly ascending, or a segment starting before the previous one ends;
   - an `interpolation_mode` that is not an enumerator;
   - `loops` true with `loop_start_subtick` at or after the table end.
3. A `linear` or `catmull_rom` segment on a table that allows only `step`.
4. A `step` segment of more than one value whose `interval_subticks * tick_multiplier` is below `subticks_per_base_tick`, since a value shorter than one effective tick could never be heard.
5. A `ValueRange` with a non-finite bound, with `min` above `max`, or whose width `max - min` is not finite.
6. A voice holding both a `pulse_width` and a `pulse_width_delta` table.
7. `grit.tick_multiplier` outside `[1, 16]`.
8. A `grit.bit_depths` entry outside `[1, 24]`.
9. A `ring_mod_source` or `hard_sync_source` outside `[0, voice_count - 1]`, or equal to its own voice's index.
10. A `pitch_slide_bounds` or `pulse_width_delta_bounds` with a non-finite bound, with a bound outside the field's own default range (`[0, 1]` for `pulse_width_delta_bounds`), or with `max` below `min` when both are present.

**Every other range in the tables above is a precondition, not a check.** The library does not validate them, because a consumer's loader validates its authored content once already, and repeating that work on every start would duplicate it. A value outside a stated range still produces a defined result rather than undefined behavior: the emitted parameter block is clamped regardless of what the program held. A consumer that skips validation gets a wrong sound, never a crash.

## Lifetime

A sound program is **not copied** when an instance starts. The instance holds a pointer to it. Keep the program, and every array it points to, alive and unchanged until `synthRelease` for that instance, `synthConfigure`, or `synthDestroy`.

Replacing or reloading the source a program was built from has no effect on an instance already running against it. The next start picks up the new program; the running one finishes on the old one.
