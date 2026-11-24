#include "glirps/Environment.hpp"

#include "ScopedAllocationCounter.hpp"
#include "EnvironmentTestSupport.hpp"
#include "impl/Logger.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <format>
#include <string>
#include <string_view>

// Counts how many times the formatter below runs, so a test can prove that a
// disabled log message is never formatted.
namespace
{
	struct CountedValueLogger
	{
		int value = 0;
		// Incremented each time the value is formatted.
		std::size_t* format_calls = nullptr;
	};

	void ignoreMessageLogger(void*, glirps::LogSeverity, const char*, std::size_t) noexcept {}
}

template<>
struct std::formatter<CountedValueLogger> : std::formatter<int>
{
	auto format(const CountedValueLogger& counted, std::format_context& context) const
	{
		++*counted.format_calls;
		return std::formatter<int>::format(counted.value, context);
	}
};

namespace
{
	using glirps::test_support::isRecord;
	using glirps::test_support::RecordingLog;

	constexpr std::size_t oversize_factor_logger = 4;
}

TEST(LoggerTests, Log_SeverityAtLeastMinimum_ReportsMessage)
{
	// ARRANGE
	RecordingLog log;
	const glirps::Environment environment = log.makeEnvironment(glirps::LogSeverity::warning);
	const glirps::impl::Logger logger(environment.logger);

	// ACT
	logger.log(glirps::LogSeverity::debug, "debug {}", 1);
	logger.log(glirps::LogSeverity::warning, "warning {}", 2);
	logger.log(glirps::LogSeverity::error, "error {}", 3);

	// ASSERT
	EXPECT_THAT(log.records,
		testing::ElementsAre(isRecord(glirps::LogSeverity::warning, "warning 2"),
			isRecord(glirps::LogSeverity::error, "error 3")));
}

TEST(LoggerTests, Log_DisabledSeverity_DoesNotFormat)
{
	// ARRANGE
	RecordingLog log;
	const glirps::Environment environment = log.makeEnvironment(glirps::LogSeverity::error);
	const glirps::impl::Logger logger(environment.logger);
	std::size_t format_calls = 0;

	// ACT
	logger.log(glirps::LogSeverity::warning, "value {}", CountedValueLogger{.value = 7, .format_calls = &format_calls});

	// ASSERT
	EXPECT_EQ(format_calls, 0U);
	EXPECT_TRUE(log.records.empty());
}

TEST(LoggerTests, Log_EnabledSeverity_FormatsOnce)
{
	// ARRANGE
	RecordingLog log;
	const glirps::Environment environment = log.makeEnvironment(glirps::LogSeverity::warning);
	const glirps::impl::Logger logger(environment.logger);
	std::size_t format_calls = 0;

	// ACT
	logger.log(glirps::LogSeverity::warning, "value {}", CountedValueLogger{.value = 7, .format_calls = &format_calls});

	// ASSERT
	EXPECT_EQ(format_calls, 1U);
	ASSERT_EQ(log.records.size(), 1U);
	EXPECT_EQ(log.records.front().message, "value 7");
}

TEST(LoggerTests, Log_MessageLongerThanBuffer_IsTruncatedAndTerminated)
{
	// ARRANGE
	RecordingLog log;
	const glirps::Environment environment = log.makeEnvironment(glirps::LogSeverity::debug);
	const glirps::impl::Logger logger(environment.logger);
	const std::string long_text(glirps::impl::log_buffer_size * oversize_factor_logger, 'x');

	// ACT
	logger.log(glirps::LogSeverity::error, "{}", std::string_view(long_text));

	// ASSERT
	ASSERT_EQ(log.records.size(), 1U);
	EXPECT_EQ(log.records.front().message.size(), glirps::impl::log_buffer_size - 1);
	EXPECT_TRUE(log.records.front().terminated);
}

TEST(LoggerTests, Log_EnabledMessage_AllocatesNothing)
{
	// ARRANGE
	// A callback that records nothing, so only the logger could allocate.
	const glirps::impl::Logger logger(glirps::LogCallbacks{.context = nullptr,
		.log = &ignoreMessageLogger,
		.min_severity = glirps::LogSeverity::debug});
	std::size_t allocations = 0;

	// ACT
	{
		const glirps::test_support::ScopedAllocationCounter counter;
		logger.log(glirps::LogSeverity::error, "pool {} voices {}", 4U, 8U);
		allocations = counter.getAllocationCount();
	}

	// ASSERT
	EXPECT_EQ(allocations, 0U);
}
