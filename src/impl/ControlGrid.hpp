#pragma once

#include "glirps/constants.hpp"

#include <cstdint>

namespace glirps::impl
{
	// Places control ticks on the sample timeline at the rate `configure`
	// fixed: one global base grid at `control_grid_base_rate_hz`, with each
	// sound definition ticking at an integer multiple of it so all sounds
	// stay phase-aligned. `tick_index`
	// counts effective ticks, at `control_grid_base_rate_hz * tick_multiplier`,
	// not base ticks.
	//
	// Every position is computed as an absolute value from the tick index,
	// with the integer division performed last, so the placement error is
	// bounded below one sample and never accumulates. Computing a per-tick
	// increment and multiplying it instead loses the remainder once and then
	// scales that loss by the tick index: at 44.1 kHz with a tick multiplier
	// of 16 the exact spacing is 55.125 frames, a truncated increment gives
	// 55, and the render drifts 2.27 ms per second without bound.
	//
	// A tick is therefore not generally a whole number of `sub_block_frame_count`
	// frames, and is not required to be: at 44.1 kHz a base tick is 882 frames,
	// which is not a multiple of `sub_block_frame_count` at any tick
	// multiplier. `subBlockForTick` resolves a tick to the sub-block
	// containing it, which is also the rule for commands applied to control
	// ticks, given that rendering runs in fixed sub-blocks of
	// `sub_block_frame_count` frames.
	struct ControlGrid
	{
		// Output sample rate the grid places ticks against, as fixed by
		// `Synth::configure`. Zero is not a usable grid.
		std::uint32_t sample_rate_hz = 0;

		// Sample position of `tick_index`, counting from tick 0 at sample 0.
		// The index is a 64-bit value, and the caller computes it as a 64-bit
		// product rather than a truncated 32-bit one.
		[[nodiscard]] std::uint64_t sampleForTick(std::uint64_t tick_index,
			std::uint32_t tick_multiplier) const noexcept
		{
			const std::uint64_t numerator = static_cast<std::uint64_t>(tick_index) * sample_rate_hz;
			const std::uint64_t denominator = static_cast<std::uint64_t>(control_grid_base_rate_hz) * tick_multiplier;
			return numerator / denominator;
		}

		// Samples spanned by the tick at `tick_index`, as the distance to the
		// next tick. Derived from two absolute positions rather than from a
		// rounded rate, so consecutive spans differ by at most one sample and
		// their running total stays exact.
		[[nodiscard]] std::uint64_t framesBetweenTicks(std::uint64_t tick_index,
			std::uint32_t tick_multiplier) const noexcept
		{
			return sampleForTick(tick_index + 1, tick_multiplier) - sampleForTick(tick_index, tick_multiplier);
		}

		// Index of the `sub_block_frame_count` boundary containing this tick's
		// position, which is where a renderer applies it, since rendering runs in
		// fixed sub-blocks.
		// Monotonic but not strictly so: an effective tick shorter than one
		// sub-block puts two ticks in the same sub-block, and the later one
		// wins there.
		[[nodiscard]] std::uint64_t subBlockForTick(std::uint64_t tick_index,
			std::uint32_t tick_multiplier) const noexcept
		{
			return sampleForTick(tick_index, tick_multiplier) / sub_block_frame_count;
		}
	};
}
