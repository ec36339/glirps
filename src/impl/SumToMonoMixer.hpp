#pragma once

#include "Mixer.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <ranges>
#include <span>

namespace glirps::impl
{
	// Default `Mixer`: one channel, zeroing the output in `beginBlock` and
	// adding each submitted instance block into it.
	class SumToMonoMixer : public Mixer
	{
	public:
		// Returns 1: this mixer writes a single mono channel.
		[[nodiscard]] std::uint32_t getChannelCount() const noexcept override { return 1; }

		// Takes `output` as this block's destination and zeroes it, so each
		// submitted instance adds into a clean buffer.
		void beginBlock(std::span<float> output, std::uint32_t frame_count) noexcept override
		{
			output_ = output;
			frame_count_ = frame_count;
			std::fill(output_.begin(), output_.end(), 0.0f);
		}

		// Adds one instance's rendered `mono` block into the output buffer.
		void submitInstance(ChipInstanceHandle, std::span<const float> mono) noexcept override
		{
			const std::span<float> block = output_.first(frame_count_);
			std::ranges::transform(block, mono, block.begin(), std::plus<float>{});
		}

		// No-op: summing leaves nothing to finalize.
		void endBlock() noexcept override {}

	private:
		std::span<float> output_;
		std::uint32_t frame_count_ = 0;
	};
}
