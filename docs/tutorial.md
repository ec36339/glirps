# Tutorial: A First Sound

This tutorial builds a small program that renders a looping three-note arpeggio and writes the samples to a file you can listen to. Each step adds a few lines to one source file. Put the blocks together in order and you have the whole program.

It assumes the project already links `glirps::glirps`, as described under *Using GLIRPS* in the repository's README. The program uses `std::println`, so compile it as C++23.

## Include the API

`glirps.hpp` declares the synth and everything a sound program is made of. The rest is the standard library. The `using` directive keeps the code short.

```cpp
#include <glirps/glirps.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <print>
#include <utility>
#include <vector>

using namespace glirps;
```

## Pick a sample rate

GLIRPS evaluates sounds on a 50 Hz grid, so each tick covers 20 ms. At 48 kHz that is 960 frames. Rendering works in sub-blocks of 64 frames, and 960 is exactly 15 of them, so the program can render one tick at a time.

```cpp
constexpr std::uint32_t sample_rate_hz = 48000;
constexpr std::uint32_t frames_per_tick = sample_rate_hz / control_grid_base_rate_hz;
```

## Describe the voice

A sound has one to four voices. This one has a single pulse voice with a short attack, a decay to 60% level, and a quarter-second release. The pulse width keeps its default of 0.5, a square wave.

```cpp
int main()
{
	VoiceProgram voice;
	voice.waveform = Waveform::pulse;
	voice.envelope = {.attack_seconds = 0.002f,
		.decay_seconds = 0.2f,
		.sustain_level = 0.6f,
		.release_seconds = 0.25f};
```

## Add the arpeggio

A table changes a parameter over time. The arpeggio table holds semitone offsets from the base pitch: here 0, 4, and 7, a major chord. One segment steps through the three values, one per tick (`subticks_per_base_tick` subticks), and `loops` repeats it for as long as the sound plays.

Each value of a numeric table is a range from which the library draws at random. Equal bounds make it a fixed value.

```cpp
	const ValueRange<std::int32_t> chord[] = {{0, 0}, {4, 4}, {7, 7}};
	const Segment steps{.value_count = 3, .interval_subticks = subticks_per_base_tick};

	voice.tables.has_arpeggio = true;
	voice.tables.arpeggio = {.segments = &steps,
		.segment_count = 1,
		.values = chord,
		.value_count = 3,
		.loops = true};
```

## Build the sound program

The program sets the base pitch, C-5 (60 semitones above C-0), and when the gate releases. Times in a program are in subticks, 16 to a tick, so 20 ticks is 0.4 s. The sound then fades for the voice's release time and ends on its own.

The program points to `voice`, which points to `chord` and `steps`. GLIRPS copies none of them, so they must stay alive until the instance is released.

```cpp
	SoundProgram program;
	program.base_pitch = {.semitone = 60};
	program.gate = {.has_release_subtick = true, .release_subtick = 20 * subticks_per_base_tick};
	program.voices = &voice;
	program.voice_count = 1;
```

## Create the synth

`synthCreate` takes the sizes of its pools: how many sounds can play at once, and how many voices they may use in total. One of each is enough here. `synthConfigure` sets the output sample rate. Until it is called, the synth produces no parameter blocks.

```cpp
	const CreateResult created = synthCreate(1, 1);
	if (!created)
		return 1;
	const SynthHandle synth = created.synth;
	synthConfigure(synth, sample_rate_hz, default_oversampling_factor);
```

## Start the sound

`synthStart` returns a handle to the playing sound, or an error if the program is invalid or the pools are full. The seed drives every random value. Within one process, the same program and seed give the same samples.

```cpp
	const StartResult started = synthStart(synth, program, /*seed*/ 1);
	if (!started)
	{
		std::println(stderr, "synthStart failed: {}", std::to_underlying(started.error));
		synthDestroy(synth);
		return 1;
	}
```

## Render tick by tick

Each pass of the loop is one tick. `synthEvaluateTick` evaluates the tables and produces a parameter block. With the default grit profile that is exactly one block per tick. `synthApplyBlock` hands it to the renderer, and `synthRender` writes the tick's 960 frames.

`synthIsComplete` turns true once the release has faded out. A sound without `has_release_subtick` waits for `synthSetGate` and never completes on its own, so a real program would also cap the loop.

```cpp
	std::vector<float> samples;
	while (!synthIsComplete(synth, started.instance))
	{
		ParameterBlock block;
		if (synthEvaluateTick(synth, started.instance, {&block, 1}) == 1)
			synthApplyBlock(synth, block);

		samples.resize(samples.size() + frames_per_tick);
		synthRender(synth, {samples.data() + samples.size() - frames_per_tick, frames_per_tick});
	}
```

In a game the two halves can run on different threads: evaluation where the game decides what to play, rendering in the audio callback. Here they share one loop.

## Clean up

Releasing the instance returns its slot to the pool. Destroying the synth frees everything else.

```cpp
	synthRelease(synth, started.instance);
	synthDestroy(synth);
```

## Write and report the result

The samples are mono floats where 1.0 is full scale. The program writes them unchanged to `arpeggio.f32` and prints the length and the peak level.

```cpp
	std::ofstream("arpeggio.f32", std::ios::binary)
		.write(reinterpret_cast<const char*>(samples.data()),
			static_cast<std::streamsize>(samples.size() * sizeof(float)));

	float peak = 0.0f;
	for (const float sample : samples)
		peak = std::fmax(peak, std::fabs(sample));
	std::println("{:.2f} s, peak {:.1f} dBFS",
		static_cast<double>(samples.size()) / sample_rate_hz,
		20.0 * std::log10(peak));
	return 0;
}
```

## Run it

The program prints:

```
0.66 s, peak -12.0 dBFS
```

The sound lasts 0.4 s until the release, plus the quarter-second fade, rounded up to whole ticks. A single voice at full amplitude with every gain at 1.0 peaks at about -12 dBFS. The sustain level of 0.6 holds this one at about -16 dBFS.

To listen, import `arpeggio.f32` as raw data into an audio editor such as Audacity: 32-bit float, little-endian on x86 and ARM, one channel, 48000 Hz. To write a WAV file instead, see the `hello-wav` example in `examples/`.

## Next steps

- Change the seed and give a chord value unequal bounds, for example `{4, 5}`, to hear seeded variation.
- Add a second voice, after raising the voice pool passed to `synthCreate`, or a filter sweep with a `FilterTables::cutoff` table. [The sound program](sound-program.md) lists every field and its range.
- Read [the overview](INDEX.md) for how the control side and the render side fit together, and [parameter blocks](parameter-blocks.md) for what crosses between them.
