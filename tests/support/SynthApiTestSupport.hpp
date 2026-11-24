#pragma once

#include "glirps/glirps.hpp"
#include "glirps/glirps_handles.hpp"

#include "SoundProgramTestSupport.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

// Shared helpers for the tests of the free functions in `glirps/glirps.hpp`.
namespace glirps::test_support
{
	inline constexpr std::uint32_t test_sample_rate_hz = 48000;
	inline constexpr std::uint32_t test_oversampling_factor = 16;
	inline constexpr std::uint32_t frames_per_base_tick = test_sample_rate_hz / control_grid_base_rate_hz;

	// A one-voice program with no authored release: a sustained instance the
	// consumer gates through `synthSetGate`. `storage` SHALL outlive every
	// use of the returned `SoundProgram`.
	inline SoundProgram makeSustainedProgram(TestProgramStorage& storage)
	{
		storage.program.base_pitch = Pitch{.semitone = 36, .cents = 0};
		VoiceProgram voice;
		voice.waveform = Waveform::triangle;
		voice.gain = 1.0f;
		voice.envelope = EnvelopeSpec{.attack_seconds = 0.002f,
			.decay_seconds = 0.006f,
			.sustain_level = 1.0f,
			.release_seconds = 0.006f};
		setVoices(storage, {voice});
		return storage.program;
	}

	inline float peakOf(std::span<const float> samples) noexcept
	{
		float peak = 0.0f;
		for (const float sample : samples)
			peak = std::max(peak, std::fabs(sample));
		return peak;
	}

	// Evaluates one base control tick, applies its blocks, renders one base
	// tick's worth of frames, and returns the peak of the rendered output.
	inline float renderOneBaseTick(SynthHandle synth, ChipInstanceHandle instance)
	{
		const std::uint32_t block_count = synthGetBlocksPerBaseTick(synth, instance);
		std::vector<ParameterBlock> blocks(block_count);
		const std::uint32_t written = synthEvaluateTick(synth, instance, shims::toSpan(blocks));
		for (const ParameterBlock& block : std::span<ParameterBlock>(blocks).first(written))
			synthApplyBlock(synth, block);
		std::vector<float> output(frames_per_base_tick, 0.0f);
		synthRender(synth, shims::toSpan(output));
		return peakOf(output);
	}
}
