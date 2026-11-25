# The Block Renderer

A *block renderer* renders one [chip instance](chip-instance.md)'s worth of voices and nothing else. It has no [sound program](sound-program.md), no tables, no instance pool, and no [mixer](mixer.md). The [consumer](glossary.md#consumer) drives it directly and receives mono samples. Use it when you already know what every voice should do at every moment, for example to audition a waveform, to render from your own control logic, or to measure the signal path.

To play whole sounds from sound programs, use the synth instead. Both are described in [the overview](INDEX.md#synth-and-block-renderer).

The functions are declared in `glirps_render.hpp`. The optional `glirps_handles.hpp` provides `BlockRendererOwner`, which destroys the handle on scope exit.

## Creating one

`blockRendererCreate` takes the output sample rate, the oversampling factor, and an optional [environment](INDEX.md#environment-allocation-and-logging). It returns the handle, or a falsy result if allocation failed. `blockRendererReconfigure` changes the sample rate or the oversampling factor later, and `blockRendererDestroy` frees the renderer.

Creating, reconfiguring, and destroying can allocate, so a real-time consumer calls them while the audio thread is not rendering.

## Renderer voices

A block renderer always has `max_voices_per_instance` voices, which is 4, numbered from 0. Every `voice_index` argument must be below that. An out-of-range index terminates the program.

Each voice is one oscillator, one envelope, and one gain, as described in [the chip instance manual](chip-instance.md#voices). All voices share one filter and one output gain.

## Driving it

There are two ways to change what the renderer sounds like, and they can be mixed.

**Direct setters** change one thing at a time:

| Function | What it sets |
| --- | --- |
| `blockRendererConfigureVoice` | Waveform, frequency, pulse width, gain, and noise seed of one voice. It also makes the voice take part in rendering |
| `blockRendererSeedVoiceNoise` | The noise seed of one voice, leaving everything else alone |
| `blockRendererSetVoiceFrequency` | The frequency of one voice, in hertz. Intended for sweeps |
| `blockRendererSetGate` | The gate of one voice. The envelope starts attack on gate on and release on gate off |
| `blockRendererSetVoiceEnvelopeSeconds` | Attack, decay, and release times in seconds, and the sustain level |
| `blockRendererSetVoiceEnvelope` | The same, chosen from a table of presets. See [envelope presets](#envelope-presets) |
| `blockRendererSetRingModSource`, `blockRendererSetHardSyncSource` | [Ring modulation](chip-instance.md#ring-modulation) and [hard sync](chip-instance.md#hard-sync) routing for one voice |
| `blockRendererSetFilter` | Cutoff, resonance, saturation, and one filter response |
| `blockRendererSetFilterCutoff`, `blockRendererSetFilterSaturation` | Only the cutoff, or only the saturation |
| `blockRendererSetOutputGain` | The shared output gain |

Frequencies are in hertz. [`frequencyHertz`](../include/glirps/frequency.hpp) converts a note and cents to hertz. Ranges are the ones in [parameter blocks](parameter-blocks.md#contents); treat them as preconditions of the setters.

`blockRendererSetFilter` selects a single response. To sum several responses, apply a [parameter block](parameter-blocks.md), whose filter modes are a set.

**Parameter blocks** set everything a block carries at once:

- `blockRendererApplyBlock` installs a block the way the synth does: waveform, frequency, pulse width, filter modes, and gate step immediately, while cutoff, resonance, voice gain, and output gain [ramp over one sub-block](parameter-blocks.md#applying-a-block).
- `blockRendererApplyBaseBlock` snaps every field with no ramp. Use it to establish a starting state, so that the first samples are not a fade from the previous values.

The block's `target` field names a synth instance and is not used here. A block carries no envelope times and no filter saturation, and applying one never changes them. The envelope setters and `blockRendererSetFilterSaturation` set those.

## Envelope presets

`blockRendererSetVoiceEnvelope` takes three indices, 0 to 15, for attack, decay, and release. They select from 16 presets. The attack time is the time to reach 99% of full level, as for `EnvelopeSpec::attack_seconds`. The decay and release times of an index are three times its attack time.

| Index | Attack | Decay and release | Index | Attack | Decay and release |
| --- | --- | --- | --- | --- | --- |
| 0 | 2 ms | 6 ms | 8 | 167 ms | 501 ms |
| 1 | 3.48 ms | 10.4 ms | 9 | 290 ms | 870 ms |
| 2 | 6.04 ms | 18.1 ms | 10 | 504 ms | 1.51 s |
| 3 | 10.5 ms | 31.5 ms | 11 | 876 ms | 2.63 s |
| 4 | 18.3 ms | 54.9 ms | 12 | 1.52 s | 4.56 s |
| 5 | 31.7 ms | 95.1 ms | 13 | 2.65 s | 7.95 s |
| 6 | 55.2 ms | 165.6 ms | 14 | 4.60 s | 13.8 s |
| 7 | 95.9 ms | 287.7 ms | 15 | 8.00 s | 24.0 s |

An index of 16 or more is ignored, and the envelope keeps its previous settings. The attack, decay, and release shapes and the meaning of the sustain level are the ones in [gain and the envelope](chip-instance.md#gain-and-the-envelope).

## Rendering

`blockRendererRender` fills a mono buffer. Its size must be a whole number of [sub-blocks](glossary.md#sub-block), that is a multiple of `sub_block_frame_count`. It allocates nothing, takes no lock, and makes no system call. `blockRendererRenderOne` advances a single sample and returns it, for a caller that changes a parameter every sample.

`blockRendererIsComplete` is true once every voice has finished. A voice that has never been gated counts as finished, so a freshly created renderer reports complete until a voice is gated on. `blockRendererResetState` returns the renderer to a cleared state: every voice inactive, oscillator and envelope zeroed, filter and decimator history cleared. `blockRendererGetDecimatorGroupDelayInFrames` reports the decimator's group delay in output frames, so that renders at different oversampling factors can be aligned for measurement.

The output follows the same level convention as the synth: with every gain at 1.0, a voice at full waveform amplitude peaks at the [nominal source level](glossary.md#nominal-source-level). Nothing limits the result.

## Threading and real-time use

A block renderer has no lock of its own and belongs to one thread at a time. Two calls on the same handle must not overlap, and handing a handle to another thread needs a happens-before relation that the consumer provides. Different handles are independent.

A consumer that renders on an audio thread calls every function on the handle from that thread. The exceptions are setup before the thread starts using the handle, such as `blockRendererCreate` and `blockRendererConfigureVoice`, and `blockRendererDestroy` after it has stopped. To change parameters from another thread, pass values to the audio thread, for example as `ParameterBlock` values for `blockRendererApplyBlock`, instead of calling the setters from there.

Unlike `synthRender`, `blockRendererRender` does not set flush-to-zero or denormals-are-zero. A consumer that wants either sets it on the render thread.

## Renderer determinism

Noise is the only random source. It comes from each voice's seed, so the same calls with the same seeds produce the same samples in one process, as for [the synth](INDEX.md#determinism).
