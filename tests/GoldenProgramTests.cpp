#include "SynthApiTestSupport.hpp"
#include "golden_programs.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	// A rendering whose peak is not above this counts as silent.
	constexpr float audible_peak_threshold = 0.01f;
}

// Guards the golden set itself: a case whose feature has no audible effect, or
// that renders silence, would test nothing. Platform-independent.
TEST(GoldenProgramTests, Render_AllPrograms_AreAudibleAndDistinct)
{
	// ARRANGE
	std::vector<std::string> failed_cases;
	std::vector<std::string_view> silent_cases;
	std::set<std::uint64_t> hashes;

	// ACT
	for (const glirps::test_support::GoldenCase& golden : glirps::test_support::golden_cases)
	{
		const auto samples = glirps::test_support::renderGoldenCase(golden);
		if (!samples)
		{
			failed_cases.push_back(std::string(golden.name) + ": " + glirps::test_support::describe(samples.error()));
			continue;
		}
		if (glirps::test_support::peakOf(*samples) <= audible_peak_threshold)
			silent_cases.push_back(golden.name);
		hashes.insert(glirps::test_support::hashPcm16(*samples));
	}

	// ASSERT
	EXPECT_THAT(failed_cases, testing::IsEmpty());
	EXPECT_THAT(silent_cases, testing::IsEmpty());
	EXPECT_EQ(hashes.size(), glirps::test_support::golden_cases.size());
}
