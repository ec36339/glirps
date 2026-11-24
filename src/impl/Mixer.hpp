#pragma once

#include "glirps/ChipInstanceHandle.hpp"

#include <cstdint>
#include <span>

namespace glirps::impl
{
	// Receives each chip instance's rendered block and produces the output
	// buffer. Called from `Synth::render`, so every override inherits that
	// entry point's constraints: no allocation, no lock, no syscall, no
	// logging.
	//
	// This is the seam a consumer replaces to add panning, distance
	// attenuation, bus routing, or a master limiter. `glirps` supplies
	// `SumToMonoMixer` so a consumer that wants none of that installs nothing.
	class Mixer
	{
	public:
		Mixer() = default;
		virtual ~Mixer() = default;
		Mixer(const Mixer&) = delete;
		Mixer& operator=(const Mixer&) = delete;
		Mixer(Mixer&&) = delete;
		Mixer& operator=(Mixer&&) = delete;

		// Returns the interleaved channels this mixer writes per frame. Fixed
		// for the mixer's lifetime.
		[[nodiscard]] virtual std::uint32_t getChannelCount() const noexcept = 0;

		// Begins one sub-block. `output` holds
		// `frame_count * getChannelCount()` samples and its previous contents
		// are undefined. Every sample SHALL be written by the time `endBlock`
		// returns.
		virtual void beginBlock(std::span<float> output, std::uint32_t frame_count) noexcept = 0;

		// Submits one chip instance's mono output for the block in progress.
		// Called once per live instance, in unspecified order. `mono` holds
		// `frame_count` samples and is valid only for the duration of the call.
		virtual void submitInstance(ChipInstanceHandle instance, std::span<const float> mono) noexcept = 0;

		// Completes the block begun by `beginBlock`.
		virtual void endBlock() noexcept = 0;
	};
}
