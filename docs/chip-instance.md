# Chip Instance Manual

A *chip instance* is one playing sound. It is a small fixed group of voices sharing one filter and one output gain, named after the hardware it models, where the filter and master volume are per chip rather than per voice.

One sound effect playing is one chip instance. Playing the same effect three times at once is three instances, each with its own oscillator phases, envelope levels, filter state, and seed. Polyphony never comes from adding voices to an instance: the voice count is fixed for the instance's lifetime, taken from the sound it plays, and is at most four.

Every parameter named here is a field of a [sound program](sound-program.md), which is where its unit and range are stated.

## Structure

```
chip instance
├── voice 0 ──┐   oscillator ──► waveform ──► optional ring mod / hard sync
├── voice 1 ──┤   envelope ──► voice gain
├── voice 2 ──┤
├── voice 3 ──┘
│        │
│        ▼   sum of this instance's voices
├── shared filter: lowpass + bandpass + highpass + notch, summed over the
│                  selected set, with optional soft saturation
│        │
│        ▼
└── shared output gain ──► one mono block
```

The instance is also the unit of everything downstream: spatial placement and mix routing attach here, never to an individual voice.

## What an instance owns

An instance owns only its **sounding state**:

- Per voice: oscillator phase, noise state, envelope phase and level, and the current value of its gain.
- Shared: filter state and the current values of cutoff, resonance, and output gain.

It owns no table position, no tick rate, and no grid phase. Those live on the [control side](glossary.md#control-side), which is why a chip instance can be rendered on a different thread from the one that decides what it plays. What reaches it is a stream of [parameter blocks](parameter-blocks.md), each carrying a complete set of values for one instant.

## Lifetime states

An instance moves through these states in order. Nothing skips backwards.

| State | What is true | What ends it |
| --- | --- | --- |
| **Started** | A pool slot is reserved, the program is attached, and a handle identifies the instance. Every voice is gated on and renders from its base values, so an instance that has received no parameter block yet is already making the right sound rather than silence | The first effective tick |
| **Delayed** | An onset delay or a voice's own `VoiceProgram::start_subtick` holds voices inactive: no attack, gate reads false, and that voice's tables are not evaluated | Reaching the time the delay names. A voice's tables then start at their own time 0 |
| **Sounding** | Voices are gated on. Tables are evaluated, parameter blocks arrive, the envelopes run attack, decay, and sustain | The gate closing |
| **Releasing** | The gate has closed, every envelope is in release. Tables keep running and blocks keep arriving; a release is not a freeze | Every envelope reaching zero |
| **Complete** | Every active voice's envelope has finished. The instance produces silence and is ready to be reclaimed | The consumer releasing the slot |
| **Released** | The slot is back in the [instance pool](glossary.md#instance-pool), and `synthStart` can reuse it after the next `synthRender`. The handle that named this instance is now expired and is recognised as such rather than naming whatever occupies the slot next | Nothing |

Two details govern when **Complete** is reached:

1. A voice that never gated on counts as finished. Without that, a voice held inactive past its instance's release time would keep the instance alive forever.
2. Completion is only latched after the chip-wide gate has closed, so an instance cannot report complete before it has sounded.

A sound whose `GateProgram::release_subtick` is absent never reaches **Complete** on its own. Whatever triggered it must close the gate.

Completion is reported for the whole instance. A voice that finishes ahead of the others does not return its voice slot to the pool: all of an instance's voice slots, like its instance slot, are freed together when the consumer releases it.

Starting a sound can fail when the pools are exhausted. The library never steals a slot from a running instance and never queues the request, so a consumer that has no slot or voice to spare simply drops the trigger.

## Voices

A voice is one oscillator, one envelope, and one gain. Four parameters describe what it emits, and all four can change every effective tick.

### Waveform

| Waveform | Character |
| --- | --- |
| `Waveform::triangle` | Odd harmonics falling as the square of their order. The softest of the four, and the usual carrier for ring modulation |
| `Waveform::sawtooth` | All harmonics falling as their order. The brightest tonal shape, and the best test signal for a filter sweep |
| `Waveform::pulse` | Rectangular, with harmonic content set by the pulse width |
| `Waveform::noise` | Pseudo-random, clocked by the voice's oscillator |

Waveform changes are immediate steps with no smoothing, which is what makes a `VoiceTables::waveform` table read as a hard timbral switch rather than a crossfade.

The noise register is clocked by the voice's own oscillator phase, so **noise is pitched, never white**. Raising the pitch shifts the register faster and the noise gets brighter; lowering it gives a coarse rumble.

### Frequency

Frequency is the one value no sound program holds. It is assembled every tick from these contributions:

1. `SoundProgram::base_pitch`, as a note index plus cents.
2. The seeded per-instance pitch variation, when `SoundProgram::scale` is non-empty.
3. The current `VoiceTables::arpeggio` value, in semitones.
4. The current `VoiceTables::pitch_offset` value, in fractional semitones.
5. The accumulated `VoiceTables::pitch_slide`, in cents.
6. The external `pitch_cents`, in cents, when the consumer supplies it (see [External per-tick control](#external-per-tick-control)).

The result is clamped to the range from zero to half the sample rate. Clamping rather than wrapping is deliberate: a long slide reaching the end of the range is content behaving as written.

A voice at **zero frequency holds its phase and stops clocking its noise register**, but its envelope and gate are unaffected. A sound slid down to zero fades out under its envelope rather than stopping abruptly.

Pitch steps are never smoothed. That is deliberate and is a large part of the period character: an arpeggio table stepping at 50 Hz is meant to be heard as steps. An interpolated `pitch_offset` table is sampled once per effective tick too, so its glide is heard as a fine staircase at the effective tick rate.

### Pulse width

Read only under `Waveform::pulse`, and ignored otherwise, though it always carries a value in range. It starts at `VoiceProgram::pulse_width` and is either replaced by a `VoiceTables::pulse_width` table or moved by an accumulating `VoiceTables::pulse_width_delta` table, never both.

- `0.5` is a square wave, hollow, odd harmonics only.
- Moving away from `0.5` in either direction thins the sound and adds even harmonics.
- `0.0` and `1.0` are both a **constant level, not silence**. There are no edges, so nothing is audible, but the output sits at one rail. Sweeping a pulse width across the full range clicks at both ends.

### Gain and the envelope

A voice's level is its envelope multiplied by its gain. The envelope is the primary control; gain is for balancing voices against each other.

The envelope has five phases:

| Phase | Behaviour |
| --- | --- |
| `idle` | Before the first gate on. Silent and stationary, and distinct from an attack that has only just started |
| `attack` | **Linear** rise from the current level to full. `EnvelopeSpec::attack_seconds` is the time to reach 99% of full |
| `decay` | **Exponential** fall from full to the sustain level. `EnvelopeSpec::decay_seconds` is the time to come within 1% of `EnvelopeSpec::sustain_level`, or to reach 0.001 of full level when the sustain level is 0 |
| `sustain` | Held at `EnvelopeSpec::sustain_level` for as long as the gate is on. A level, not a time |
| `release` | **Exponential** fall to zero from wherever the level was. `EnvelopeSpec::release_seconds` is the time to fall from the sustain level to 0.001 of full level (-60 dB), where the voice counts as silent |

The gate is **edge-triggered**: repeating the same gate value does nothing. Closing a gate that never opened leaves the voice idle rather than starting a release from silence.

Gain changes ramp over one sub-block, about 1.3 ms, so a `VoiceTables::gain` value cannot click.

## Ring modulation

A voice whose `VoiceProgram::ring_mod_source` is set has its own output sample multiplied, every sample, by the **raw oscillator output** of the source voice. The source's envelope, its gain, and its own ring modulation are not part of that product.

The consequence is the one that matters in practice: **the source voice can be set to gain 0.0 so it is silent in the mix while still modulating**. That is the normal way to author it, since a modulator is rarely wanted as an audible tone of its own.

What it sounds like depends entirely on the frequency ratio between the two voices:

| Ratio | Result |
| --- | --- |
| Unison | Squares the waveform against itself: an octave up and duller |
| Simple, such as 2:1 or 3:2 | Sidebands stay near the harmonic series, so the result is still pitched, hollower than the carrier |
| Detuned by tens of cents | The difference tone drops to a few hertz and the result beats slowly |
| Inharmonic | Sum and difference tones sit off the harmonic series: bells, clangs, and metal |
| Modulator below about 20 Hz | Heard as tremolo rather than as new partials, at twice the modulator's rate, since the product dips at both zero crossings |
| Noise modulator | The carrier's amplitude is scrambled per sample: pitched material turned into grit |

## Hard sync

A voice whose `VoiceProgram::hard_sync_source` is set has its phase **reset every time the source oscillator completes a cycle**. The perceived pitch becomes the source's, while the timbre is set by the synced voice's own frequency.

Sweeping the synced voice's frequency upward while the source holds a fixed pitch is the classic result: a sweeping, tearing timbre at a constant pitch. As with ring modulation, the source is usually set to gain 0.0 so only the synced voice is heard.

## The shared filter

One filter serves every voice. It sits after the voices are summed and before the output gain, so it colours the whole instance and cannot be applied to one voice alone.

It is a state variable filter with four responses: lowpass, bandpass, highpass, and notch. The selected set is **summed**, not switched:

| Selection | Effect |
| --- | --- |
| One response | The ordinary case |
| Several | Their sum, which is what makes combinations such as lowpass plus notch cheap |
| None | The input passes through unfiltered. A defined state, not silence |

Three parameters drive it:

- **Cutoff**, `FilterProgram::cutoff_hz` and the `FilterTables::cutoff` values that replace it, clamped at run time to just under half the sample rate. Smoothed over one sub-block.
- **Resonance**, `FilterProgram::resonance_q` and the `FilterTables::resonance` values that replace it, a Q from 0.5 to 20, where the top of the range is where the filter approaches self-oscillation. Smoothed over one sub-block. High Q on a noise source turns noise into a pitch.
- **Saturation**, `FilterProgram::saturation`, soft saturation in the feedback path. **Fixed for the instance's whole life**: no parameter block carries it, so it cannot be automated by a table.

## Output gain and level

The shared output gain is applied last, after the filter, and is smoothed over one sub-block.

An instance rendering at full waveform amplitude with every gain at 1.0 produces a nominal peak of **-12 dBFS**. `SoundProgram::output_gain` and every `VoiceProgram::gain` is a linear multiplier relative to that nominal, not a fraction of full scale.

Authored gains lie in `[0, 1]`, so a sound never boosts itself above the nominal level. The only way to exceed it is the external `gain_multiplier`. Further level management belongs downstream in [the mixer stage](mixer.md).

## External per-tick control

An instance can be driven from game state rather than from its own tables. The consumer passes a flagged `ControlParams` aggregate to `synthStart` (establishing the instance's initial control state) or `synthUpdateControlParams` (revising it per tick, taking effect at the next `synthEvaluateTick`). A `ControlFlag` bit in the struct's `flags` names which fields the call carries; an unflagged field keeps the instance's current value. A non-finite flagged value is ignored rather than stored.

Three fields can be driven this way:

- **`pitch_cents`** is added to the instance's assembled pitch between the accumulated pitch slide and the grit stage, shifting every voice of the instance. This lets a charge level, for example, drive a rising or falling sweep without a table.
- **`cutoff_hz`** overrides the shared filter's authored cutoff. A flagged zero is a genuine override that clamps to the 1 Hz floor, not a signal to restore the authored value.
- **`gain_multiplier`** scales the chip output gain, letting a held cue follow a continuous gameplay quantity such as engine throttle. A flagged 1 leaves the level unchanged, 0 silences the instance without releasing its gate. The product is clamped to the block's gain range, which extends above 1.

The values persist until the next call, a reset, or instance release. `synthUpdateControlParams` applies to sustained and one-shot instances alike, and has no effect on an expired handle.

`synthUpdateControlParams`'s `reset_flags` names fields to instead restore to their authored state: `pitch_cents` to 0, `cutoff_hz` to the shared filter's authored cutoff (clearing the override), and `gain_multiplier` to 1. A field named in both `flags` and `reset_flags` in the same call resets; the `flags` value for that field is ignored. `synthStart` does not read `reset_flags`, since a freshly started instance already begins at the authored state it would restore.

## Instance determinism

Everything random in an instance comes from the seed it was started with:

- The noise shift register's initial state.
- The seeded pitch variation, which draws one step and snaps it to a degree of `SoundProgram::scale`. With that list empty, no variation is applied at all.
- Every random table value, drawn once per pass through its table and redrawn on each new pass of a looping table, rather than once at the start.

No clock, address, or entropy source is read. Two instances of the same sound with the same seed are identical; two with different seeds differ only in these three places.
