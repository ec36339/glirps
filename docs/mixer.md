# The Mixer Stage

The *mixer* is the last stage inside the library. Rendering walks every live [chip instance](chip-instance.md), produces one mono sub-block for each, and hands those sub-blocks to the mixer, which writes the output buffer.

It exists as a replaceable stage rather than a fixed summing step because everything spatial is deliberately outside the synthesiser. GLIRPS has no notion of a listener, a position, a distance, a bus, or a channel layout, and the mixer is the one seam where a [consumer](glossary.md#consumer) adds them without the library gaining any of it.

Two terms are used throughout. The *default mixer* is the one GLIRPS supplies. A *consumer mixer* is one the consumer installs in its place. Everything below that applies to a consumer mixer applies to the default mixer too; it is simply the least interesting instance of one.

## The default mixer

The default mixer sums every instance to mono. It reports one channel, zeroes the output buffer at the start of a sub-block, adds each submitted instance into it, and does nothing at the end.

It is installed without any action, so a consumer that wants no spatial behaviour installs nothing and gets this.

## Installing a mixer

A consumer installs a mixer with `synthSetMixer`, passing a `MixerCallbacks` struct: a `void*` context and four function pointers, `get_channel_count`, `begin_block`, `submit_instance`, and `end_block`, which map onto the channel count and the three phases below. A null pointer, or a struct with a null function pointer, selects the default mixer. `synthGetChannelCount` reports the installed mixer's channel count, so a consumer can size the buffer it passes to `synthRender`. The thread rules for each callback are in `MixerCallbacks` in the [API reference](https://ec36339.github.io/glirps/structglirps_1_1_mixer_callbacks.html).

## What a consumer mixer is for

| Concern | Why it belongs here |
| --- | --- |
| Panning | Placement is a property of the instance, and the instance's identity is what the mixer receives |
| Distance attenuation | Same reason, and it needs a listener the library has no concept of |
| Bus routing | A `SoundProgram` carries no bus. Routing attaches at the instance |
| Limiting | No stage inside the library constrains the summed result, so bounding it is the mixer's |
| Channel layout | The synthesis path is channel-unaware at every stage. The mixer decides the output topology |

The chip instance is the right granularity for all of these, which is why the seam is here and not inside a voice.

## The sub-block lifecycle

The render proceeds in sub-blocks of a fixed 64 frames, about 1.3 ms at 48 kHz. Each sub-block runs three phases in order:

1. **Begin.** The output buffer is handed over along with the frame count. Its previous contents are undefined. Every sample of it must hold a written value by the time the sub-block ends.
2. **Submit.** Once per live chip instance, in **unspecified order**, carrying that instance's mono samples and the handle identifying it. The samples are valid only for the duration of that one submission, so a consumer mixer that needs them later copies them.
3. **End.** The sub-block is finished.

The output buffer holds `frame_count` times the mixer's channel count samples, interleaved. The channel count is fixed for the mixer's lifetime and cannot change per sub-block.

Rendering **overwrites** the output rather than accumulating into it. A consumer mixer that wants to blend with whatever was in the buffer copies those contents during the begin phase, before anything is submitted.

Every instance is submitted in every sub-block from its start until the consumer releases it, including after it has completed, when its samples are silence. Nothing announces the release; a consumer mixer holding per-instance state keys it on the handle and lets entries expire once the handle is expired.

## Constraints on a consumer mixer

A mixer runs inside the render path, so it inherits that path's constraints:

- **No allocation, no lock, no syscall, no logging.** The library cannot enforce this, so it is a contract term rather than a checked one.
- **It runs on whichever thread calls the render side.** The mixer boundary is a code boundary, not a thread boundary and not a transport boundary: a call is not a queue, and installing a mixer does not move any work between threads.
- **The submitted span is borrowed.** It does not outlive the submission.

## Level and headroom

A chip instance rendering at full waveform amplitude with every gain at 1.0 peaks at a nominal **-12 dBFS**, the level every gain in a sound program is a multiplier against.

No stage inside the library constrains the summed result. How much headroom the sum needs, and whether it is bounded, is the mixer's to decide.

## What the mixer does not see

The mixer receives an instance's finished mono samples and its handle. It does not see voices, envelopes, the filter, the [sound program](sound-program.md), or [parameter blocks](parameter-blocks.md). Anything a consumer mixer needs about an instance beyond its samples, such as a world position or a bus assignment, the consumer tracks on its own side and looks up by handle.
