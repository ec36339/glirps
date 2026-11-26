# The Mixer Stage

The *mixer* is the last stage inside the library. Rendering walks every live [chip instance](chip-instance.md), produces one mono sub-block for each, and hands those sub-blocks to the mixer, which writes the output buffer.

GLIRPS has no listener, positions, buses, or channel layout. Replace the mixer to add them.

## The default mixer

The default mixer sums every instance to mono. It reports one channel, zeroes the output buffer at the start of a sub-block, adds each submitted instance into it, and does nothing at the end.

It is installed without any action, so a consumer that wants no spatial behavior installs nothing and gets this.

## Installing a mixer

A consumer installs a mixer with `synthSetMixer`, passing a `MixerCallbacks` struct: a `void*` context and four function pointers, `get_channel_count`, `begin_block`, `submit_instance`, and `end_block`, which map onto the channel count and the three phases below.

A null pointer selects the default mixer. A struct must have all four function pointers set. `synthGetChannelCount` reports the installed mixer's channel count, so a consumer can size the buffer it passes to `synthRender`.

The thread rules for each callback are in `MixerCallbacks` in the [API reference](https://ec36339.github.io/glirps/structglirps_1_1_mixer_callbacks.html).

## What a consumer mixer is for

| Concern | Why it belongs here |
| --- | --- |
| Panning | Placement is a property of the instance, and the instance's identity is what the mixer receives |
| Distance attenuation | Same reason, and it needs a listener the library has no concept of |
| Bus routing | A `SoundProgram` carries no bus. Routing attaches at the instance |
| Limiting | No stage inside the library constrains the summed result, so bounding it is the mixer's |
| Channel layout | The synthesis path is channel-unaware at every stage. The mixer decides the output topology |

## The sub-block lifecycle

The render proceeds in sub-blocks of a fixed 64 frames, about 1.3 ms at 48 kHz. Each sub-block runs three phases in order:

1. **Begin.** The output buffer is handed over along with the frame count. Its previous contents are undefined. Every sample of it must hold a written value by the time the sub-block ends.
2. **Submit.** Once per live chip instance, in **unspecified order**, carrying that instance's mono samples and the handle identifying it. The samples are valid only for the duration of that one submission, so a consumer mixer that needs them later copies them.
3. **End.** The sub-block is finished.

The output buffer holds `frame_count` times the mixer's channel count samples, interleaved. The channel count is fixed for the mixer's lifetime and cannot change per sub-block.

Every instance is submitted in every sub-block from its start until the consumer releases it, including after it has completed, when its samples are silence. Nothing announces the release. Index per-instance state by `instance.getSlot().get()`, store the handle with it, and reset the entry when the submitted handle differs.

## Constraints on a consumer mixer

A mixer runs inside the render path, so it inherits that path's constraints:

- **No allocation, no lock, no syscall, no logging.** The library does not check this.

## Level and headroom

See [output gain and level](chip-instance.md#output-gain-and-level).

No stage inside the library constrains the summed result. How much headroom the sum needs, and whether it is bounded, is the mixer's to decide.

## What the mixer does not see

Beyond samples and handle, the consumer tracks any per-instance data, such as position or bus, itself.
