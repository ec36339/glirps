#include "GoldenProgramTestSupport.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <format>
#include <string>

// Golden hash tests: each program is rendered through the public API, converted
// to 16-bit PCM, and hashed. A changed hash means changed sound output.
// Updating a stored hash is a deliberate change.
namespace
{
	using glirps::test_support::golden_cases;
	using glirps::test_support::GoldenCase;

	// Floating-point output depends on the compiler and on the standard and
	// math libraries, so the stored hashes hold for Clang with libc++ on
	// x86-64 Linux only.
#if defined(__linux__) && defined(__x86_64__) && defined(__clang__) && defined(_LIBCPP_VERSION)
	constexpr bool stored_hashes_apply = true;
#else
	constexpr bool stored_hashes_apply = false;
#endif

	std::string toHex(std::uint64_t value) { return std::format("0x{:016x}", value); }

	class GoldenHashTests : public testing::TestWithParam<GoldenCase>
	{
	};
}

TEST_P(GoldenHashTests, Render_GoldenProgram_MatchesStoredHash)
{
	if (!stored_hashes_apply)
		GTEST_SKIP() << "Golden hashes are checked with Clang and libc++ on x86-64 Linux only.";

	// ARRANGE
	const GoldenCase& golden = GetParam();

	// ACT
	const auto samples = glirps::test_support::renderGoldenCase(golden);

	// ASSERT
	ASSERT_TRUE(samples.has_value()) << glirps::test_support::describe(samples.error());
	const std::uint64_t actual = glirps::test_support::hashPcm16(*samples);
	EXPECT_EQ(actual, golden.expected_hash) << "Actual hash of " << golden.name << ": " << toHex(actual);
}

INSTANTIATE_TEST_SUITE_P(Programs,
	GoldenHashTests,
	testing::ValuesIn(golden_cases),
	[](const testing::TestParamInfo<GoldenCase>& info) { return std::string(info.param.name); });
