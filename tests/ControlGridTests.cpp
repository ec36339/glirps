#include "impl/ControlGrid.hpp"

#include "glirps/constants.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

// `ControlGrid` places an effective tick at an absolute sample position computed
// with the integer division last, so the placement error is bounded below one
// sample and never accumulates. The tests below check that property at rates
// where `control_grid_base_rate_hz * tick_multiplier` does not divide the
// sample rate.

namespace
{
	using glirps::control_grid_base_rate_hz;
	using glirps::sub_block_frame_count;
	using glirps::impl::ControlGrid;

	// The exact rational position of `tick_index`, as a double, for comparison.
	double exactPosition(std::uint32_t rate, std::uint32_t tick_index, std::uint32_t tick_multiplier)
	{
		return static_cast<double>(tick_index) * rate
			/ (static_cast<double>(control_grid_base_rate_hz) * tick_multiplier);
	}

	TEST(ControlGridTests, SampleForTick_ZeroAndN_MapsLinearly)
	{
		// ARRANGE
		const ControlGrid grid{.sample_rate_hz = 48000};

		// ACT & ASSERT
		EXPECT_EQ(grid.sampleForTick(0, 1), 0u);
		EXPECT_EQ(grid.sampleForTick(1, 1), 960u);
		EXPECT_EQ(grid.sampleForTick(10, 1), 9600u);
	}

	TEST(ControlGridTests, FramesBetweenTicks_MultiplierOfTwo_HalvesSpacing)
	{
		// ARRANGE
		const ControlGrid grid{.sample_rate_hz = 48000};

		// ACT & ASSERT
		EXPECT_EQ(grid.framesBetweenTicks(0, 1), 960u);
		EXPECT_EQ(grid.framesBetweenTicks(0, 2), 480u);
	}

	TEST(ControlGridTests, SampleForTick_NonDividingRate_ErrorStaysBelowOneSample)
	{
		// ARRANGE
		constexpr std::uint32_t rate = 44100;
		constexpr std::uint32_t multiplier = 16;
		const ControlGrid grid{.sample_rate_hz = rate};

		// ACT & ASSERT
		for (std::uint32_t tick = 0; tick <= 5000; ++tick)
		{
			const double exact = exactPosition(rate, tick, multiplier);
			const auto actual = static_cast<double>(grid.sampleForTick(tick, multiplier));
			EXPECT_LT(exact - actual, 1.0) << "tick " << tick;
			EXPECT_GE(exact - actual, 0.0) << "tick " << tick;
		}
	}

	// The same property across several awkward rates and multipliers, including
	// a rate no device offers, since the grid accepts any `std::uint32_t`.
	TEST(ControlGridTests, SampleForTick_AwkwardRates_ErrorStaysBelowOneSample)
	{
		// ARRANGE
		constexpr std::array<std::uint32_t, 4> rates = {22050, 44100, 44101, 37800};
		constexpr std::array<std::uint32_t, 5> multipliers = {1, 3, 7, 11, 16};

		// ACT & ASSERT
		for (const std::uint32_t rate : rates)
		{
			const ControlGrid grid{.sample_rate_hz = rate};
			for (const std::uint32_t multiplier : multipliers)
			{
				for (std::uint32_t tick = 0; tick <= 2000; ++tick)
				{
					const double exact = exactPosition(rate, tick, multiplier);
					const auto actual = static_cast<double>(grid.sampleForTick(tick, multiplier));
					EXPECT_LT(exact - actual, 1.0)
						<< "rate " << rate << " multiplier " << multiplier << " tick " << tick;
					EXPECT_GE(exact - actual, 0.0)
						<< "rate " << rate << " multiplier " << multiplier << " tick " << tick;
				}
			}
		}
	}

	// Where the grid divides the rate, placement is exact.
	TEST(ControlGridTests, SampleForTick_DividingRate_IsExact)
	{
		// ARRANGE
		struct Case
		{
			std::uint32_t rate;
			std::uint32_t multiplier;
		};
		constexpr std::array<Case, 8> cases = {Case{.rate = 48000, .multiplier = 1},
			Case{.rate = 48000, .multiplier = 3},
			Case{.rate = 48000, .multiplier = 5},
			Case{.rate = 48000, .multiplier = 15},
			Case{.rate = 44100, .multiplier = 1},
			Case{.rate = 44100, .multiplier = 2},
			Case{.rate = 44100, .multiplier = 3},
			Case{.rate = 44100, .multiplier = 7}};

		// ACT & ASSERT
		for (const Case& item : cases)
		{
			const ControlGrid grid{.sample_rate_hz = item.rate};
			const std::uint64_t spacing = item.rate / (control_grid_base_rate_hz * item.multiplier);
			for (std::uint32_t tick = 0; tick <= 500; ++tick)
			{
				EXPECT_EQ(grid.sampleForTick(tick, item.multiplier), tick * spacing)
					<< "rate " << item.rate << " multiplier " << item.multiplier;
			}
		}
	}

	TEST(ControlGridTests, SampleForTick_AwkwardRates_IsStrictlyMonotonic)
	{
		// ARRANGE
		constexpr std::array<std::uint32_t, 3> rates = {22050, 44100, 44101};

		// ACT & ASSERT
		for (const std::uint32_t rate : rates)
		{
			const ControlGrid grid{.sample_rate_hz = rate};
			for (const std::uint32_t multiplier : {1U, 7U, 16U})
			{
				for (std::uint32_t tick = 0; tick < 1000; ++tick)
				{
					EXPECT_LT(grid.sampleForTick(tick, multiplier), grid.sampleForTick(tick + 1, multiplier))
						<< "rate " << rate << " multiplier " << multiplier << " tick " << tick;
				}
			}
		}
	}

	// An effective tick shorter than one sub-block puts consecutive ticks in
	// the same sub-block, so this is monotonic but not strictly so. At 44100
	// with multiplier 16 a tick is 55.125 frames, shorter than one sub-block of
	// `sub_block_frame_count` frames.
	TEST(ControlGridTests, SubBlockForTick_TickShorterThanSubBlock_IsMonotonicNotStrict)
	{
		// ARRANGE
		const ControlGrid grid{.sample_rate_hz = 44100};
		constexpr std::uint32_t multiplier = 16;

		// ACT
		bool saw_repeat = false;
		bool saw_decrease = false;
		for (std::uint32_t tick = 0; tick < 1000; ++tick)
		{
			const std::uint64_t current = grid.subBlockForTick(tick, multiplier);
			const std::uint64_t next = grid.subBlockForTick(tick + 1, multiplier);
			if (next == current)
				saw_repeat = true;
			if (next < current)
				saw_decrease = true;
		}

		// ASSERT
		EXPECT_TRUE(saw_repeat);
		EXPECT_FALSE(saw_decrease);
	}

	TEST(ControlGridTests, SubBlockForTick_ContainsThePosition)
	{
		// ARRANGE
		const ControlGrid grid{.sample_rate_hz = 44100};

		// ACT & ASSERT
		for (const std::uint32_t multiplier : {1U, 3U, 16U})
		{
			for (std::uint32_t tick = 0; tick < 500; ++tick)
			{
				const std::uint64_t position = grid.sampleForTick(tick, multiplier);
				const std::uint64_t block = grid.subBlockForTick(tick, multiplier);
				EXPECT_GE(position, block * sub_block_frame_count);
				EXPECT_LT(position, (block + 1) * sub_block_frame_count);
			}
		}
	}

	// `tick_index * sample_rate_hz` exceeds 32 bits here, so a 32-bit
	// intermediate would wrap and place the tick far too early.
	TEST(ControlGridTests, SampleForTick_LargeTickIndex_DoesNotOverflow)
	{
		// ARRANGE
		const ControlGrid grid{.sample_rate_hz = 48000};
		constexpr std::uint32_t tick = 100000;

		// ACT
		const std::uint64_t position = grid.sampleForTick(tick, 1);

		// ASSERT
		EXPECT_EQ(position, static_cast<std::uint64_t>(tick) * 48000 / control_grid_base_rate_hz);
		EXPECT_GT(position, 0xFFFFFFFFull / 48000);
	}
}
