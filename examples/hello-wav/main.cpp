// The shortest path from a GLIRPS sound program to audio: one voice, rendered
// to a WAV file.
//
// Usage: hello-wav [output.wav]

#include "WavWriter.hpp"

#include <glirps/glirps.hpp>
#include <glirps/glirps_handles.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <vector>

namespace
{
	constexpr std::uint32_t sample_rate_hz = 48000;
	// One base tick of audio, which is a whole number of render sub-blocks at
	// this sample rate.
	constexpr std::uint32_t frames_per_tick = sample_rate_hz / glirps::control_grid_base_rate_hz;
	static_assert(frames_per_tick % glirps::sub_block_frame_count == 0);
	// The gate releases after 0.3 seconds.
	constexpr std::uint32_t release_tick = 15;
	// A limit in case a sound never completes.
	constexpr std::uint32_t max_ticks = 250;
	constexpr std::uint64_t seed = 1;
	// C-4, in semitones above C-0.
	constexpr std::int32_t note = 48;

	// Renders `program` until it is complete. Empty on failure.
	std::vector<float> render(const glirps::SoundProgram& program)
	{
		// One instance with one voice.
		const glirps::CreateResult created = glirps::synthCreate(1, 1);
		if (!created)
			return {};
		// Destroys the synth on return.
		const glirps::SynthOwner synth(created.synth);
		glirps::synthConfigure(created.synth, sample_rate_hz, glirps::default_oversampling_factor);

		const auto started = glirps::synthStart(created.synth, program, seed);
		if (!started)
			return {};

		std::vector<float> samples;
		for (std::uint32_t tick = 0; tick < max_ticks && !glirps::synthIsComplete(created.synth, started.instance);
			++tick)
		{
			// With the default grit profile, each tick emits one block.
			glirps::ParameterBlock block;
			const std::uint32_t count = glirps::synthEvaluateTick(
				created.synth, started.instance, glirps::shims::Span<glirps::ParameterBlock>(&block, 1));
			if (count == 1)
				glirps::synthApplyBlock(created.synth, block);

			samples.resize(samples.size() + frames_per_tick);
			const std::span<float> tick_samples = std::span<float>(samples).last(frames_per_tick);
			glirps::synthRender(created.synth, glirps::shims::toSpan(tick_samples));
		}

		glirps::synthRelease(created.synth, started.instance);
		return samples;
	}
}

int main(int argc, char** argv)
{
	const std::filesystem::path path = argc > 1 ? argv[1] : "hello.wav";

	// A program points into storage the caller owns, here `voice`.
	glirps::VoiceProgram voice;
	voice.waveform = glirps::Waveform::pulse;
	voice.pulse_width = 0.25f;
	voice.envelope = glirps::EnvelopeSpec{.attack_seconds = 0.005f,
		.decay_seconds = 0.1f,
		.sustain_level = 0.5f,
		.release_seconds = 0.2f};

	glirps::SoundProgram program;
	program.base_pitch = glirps::Pitch{.semitone = note};
	program.gate = glirps::GateProgram{.has_release_subtick = true,
		.release_subtick = release_tick * glirps::subticks_per_base_tick};
	program.voices = &voice;
	program.voice_count = 1;

	const std::vector<float> samples = render(program);
	if (samples.empty())
	{
		std::fputs("hello-wav: rendering failed\n", stderr);
		return EXIT_FAILURE;
	}
	if (!hello_wav::writeWav(path, samples, sample_rate_hz))
	{
		std::fputs("hello-wav: writing the WAV file failed\n", stderr);
		return EXIT_FAILURE;
	}
	std::printf("Wrote %s\n", path.string().c_str());
	return EXIT_SUCCESS;
}
