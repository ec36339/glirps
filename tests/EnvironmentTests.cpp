#include "glirps/Environment.hpp"
#include "glirps/glirps.hpp"
#include "glirps/glirps_handles.hpp"
#include "glirps/glirps_render.hpp"

#include "ScopedAllocationCounter.hpp"
#include "EnvironmentTestSupport.hpp"
#include "SoundProgramTestSupport.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
	using glirps::test_support::isRecord;
	using glirps::test_support::max_tracked_blocks;
	using glirps::test_support::RecordingLog;
	using glirps::test_support::TrackingAllocator;

	constexpr std::uint32_t test_chip_instances_environment = 4;
	constexpr std::uint32_t test_voices_environment = 8;
	constexpr std::uint32_t render_block_count_environment = 4;
	constexpr std::uint32_t test_sample_rate_hz_environment = 48000;
	constexpr std::uint32_t test_oversampling_factor_environment = 16;

	glirps::SynthHandle createSynthWithPoolsEnvironment(std::uint32_t chip_instances,
		std::uint32_t voices,
		const glirps::Environment* environment)
	{
		const glirps::CreateResult created = glirps::synthCreate(chip_instances, voices, environment);
		EXPECT_TRUE(created);
		return created.synth;
	}

	glirps::SynthHandle createSynthEnvironment(const glirps::Environment* environment)
	{
		return createSynthWithPoolsEnvironment(test_chip_instances_environment, test_voices_environment, environment);
	}

	// A one-voice sustained program.
	glirps::SoundProgram makeProgramEnvironment(glirps::test_support::TestProgramStorage& storage)
	{
		storage.program.base_pitch = glirps::Pitch{.semitone = 36, .cents = 0};
		glirps::VoiceProgram voice;
		voice.waveform = glirps::Waveform::triangle;
		voice.gain = 1.0f;
		voice.envelope = glirps::EnvelopeSpec{.attack_seconds = 0.002f,
			.decay_seconds = 0.006f,
			.sustain_level = 1.0f,
			.release_seconds = 0.006f};
		glirps::test_support::setVoices(storage, {voice});
		return storage.program;
	}
}

TEST(EnvironmentTests, SynthLifecycle_WithAllocator_AllocatesOnlyThroughCallbacks)
{
	// ARRANGE
	TrackingAllocator allocator;
	const glirps::Environment environment = allocator.makeEnvironment();
	glirps::test_support::TestProgramStorage storage;
	const glirps::SoundProgram program = makeProgramEnvironment(storage);
	std::vector<float> output(glirps::sub_block_frame_count, 0.0f);

	// ACT
	std::size_t global_allocations = 0;
	{
		const glirps::test_support::ScopedAllocationCounter counter;
		const glirps::SynthOwner synth(createSynthEnvironment(&environment));
		glirps::synthConfigure(synth.get(), test_sample_rate_hz_environment, test_oversampling_factor_environment);
		EXPECT_TRUE(glirps::synthStart(synth.get(), program, 0));
		glirps::synthRender(synth.get(), glirps::shims::toSpan(output));
		global_allocations = counter.getAllocationCount();
	}

	// ASSERT
	EXPECT_EQ(global_allocations, 0U);
	EXPECT_GT(allocator.allocate_calls, 0U);
	EXPECT_EQ(allocator.getLiveCount(), 0U);
	EXPECT_FALSE(allocator.misaligned);
	EXPECT_FALSE(allocator.mismatched_free);
}

TEST(EnvironmentTests, BlockRendererLifecycle_WithAllocator_AllocatesOnlyThroughCallbacks)
{
	// ARRANGE
	TrackingAllocator allocator;
	const glirps::Environment environment = allocator.makeEnvironment();

	// ACT
	std::size_t global_allocations = 0;
	{
		const glirps::test_support::ScopedAllocationCounter counter;
		const glirps::BlockRendererCreateResult created = glirps::blockRendererCreate(test_sample_rate_hz_environment,
			test_oversampling_factor_environment,
			&environment);
		EXPECT_TRUE(created);
		glirps::blockRendererReconfigure(created.renderer,
			test_sample_rate_hz_environment,
			test_oversampling_factor_environment);
		glirps::blockRendererDestroy(created.renderer);
		global_allocations = counter.getAllocationCount();
	}

	// ASSERT
	EXPECT_EQ(global_allocations, 0U);
	EXPECT_GT(allocator.allocate_calls, 0U);
	EXPECT_EQ(allocator.getLiveCount(), 0U);
	EXPECT_FALSE(allocator.misaligned);
	EXPECT_FALSE(allocator.mismatched_free);
}

TEST(EnvironmentTests, SynthCreate_AllocatorFailsAtEachAllocation_ReportsFailureAndLeaksNothing)
{
	// ARRANGE
	std::size_t failures = 0;
	std::size_t leaks = 0;
	bool wrong_error = false;
	bool succeeded = false;

	// ACT: fail allocation 0, then 1, and so on, until a create succeeds.
	for (std::size_t fail_at = 0; !succeeded && fail_at < max_tracked_blocks; ++fail_at)
	{
		TrackingAllocator allocator;
		allocator.fail_at = fail_at;
		const glirps::Environment environment = allocator.makeEnvironment();
		const glirps::CreateResult created
			= glirps::synthCreate(test_chip_instances_environment, test_voices_environment, &environment);
		succeeded = static_cast<bool>(created);
		if (succeeded)
			glirps::synthDestroy(created.synth);
		else
		{
			wrong_error = wrong_error || created.error != glirps::CreateError::allocation_failure;
			++failures;
		}
		leaks += allocator.getLiveCount();
		wrong_error = wrong_error || allocator.mismatched_free;
	}

	// ASSERT
	EXPECT_TRUE(succeeded);
	EXPECT_GT(failures, 0U);
	EXPECT_FALSE(wrong_error);
	EXPECT_EQ(leaks, 0U);
}

TEST(EnvironmentTests, BlockRendererCreate_AllocatorFailsAtEachAllocation_ReportsFailureAndLeaksNothing)
{
	// ARRANGE
	std::size_t failures = 0;
	std::size_t leaks = 0;
	bool wrong_error = false;
	bool succeeded = false;

	// ACT
	for (std::size_t fail_at = 0; !succeeded && fail_at < max_tracked_blocks; ++fail_at)
	{
		TrackingAllocator allocator;
		allocator.fail_at = fail_at;
		const glirps::Environment environment = allocator.makeEnvironment();
		const glirps::BlockRendererCreateResult created = glirps::blockRendererCreate(test_sample_rate_hz_environment,
			test_oversampling_factor_environment,
			&environment);
		succeeded = static_cast<bool>(created);
		if (succeeded)
			glirps::blockRendererDestroy(created.renderer);
		else
		{
			wrong_error = wrong_error || created.error != glirps::BlockRendererCreateError::allocation_failure;
			++failures;
		}
		leaks += allocator.getLiveCount();
		wrong_error = wrong_error || allocator.mismatched_free;
	}

	// ASSERT
	EXPECT_TRUE(succeeded);
	EXPECT_GT(failures, 0U);
	EXPECT_FALSE(wrong_error);
	EXPECT_EQ(leaks, 0U);
}

TEST(EnvironmentTests, SynthCreate_NullEnvironment_ReturnsAWorkingSynth)
{
	// ARRANGE
	glirps::test_support::TestProgramStorage storage;
	const glirps::SoundProgram program = makeProgramEnvironment(storage);

	// ACT
	const glirps::SynthOwner synth(createSynthEnvironment(nullptr));
	glirps::synthConfigure(synth.get(), test_sample_rate_hz_environment, test_oversampling_factor_environment);

	// ASSERT
	EXPECT_TRUE(glirps::synthStart(synth.get(), program, 0));
}

TEST(EnvironmentTests, SynthCreate_NullLogFunction_ReportsInvalidArgumentsWithoutLogging)
{
	// ARRANGE
	const glirps::Environment environment{
		.allocator = glirps::AllocatorCallbacks{},
		.logger = glirps::LogCallbacks{.context = nullptr, .log = nullptr, .min_severity = glirps::LogSeverity::debug},
	};

	// ACT
	const glirps::CreateResult created = glirps::synthCreate(0, 0, &environment);

	// ASSERT
	EXPECT_FALSE(created);
	EXPECT_EQ(created.error, glirps::CreateError::invalid_arguments);
}

TEST(EnvironmentTests, SynthCreate_ZeroPools_LogsError)
{
	// ARRANGE
	RecordingLog log;
	const glirps::Environment environment = log.makeEnvironment(glirps::LogSeverity::warning);

	// ACT
	const glirps::CreateResult created = glirps::synthCreate(0, test_voices_environment, &environment);

	// ASSERT
	EXPECT_FALSE(created);
	EXPECT_THAT(log.records, testing::ElementsAre(isRecord(glirps::LogSeverity::error, "synthCreate")));
}

TEST(EnvironmentTests, SynthCreate_AllocationFailure_LogsError)
{
	// ARRANGE
	TrackingAllocator allocator;
	allocator.fail_at = 0;
	RecordingLog log;
	glirps::Environment environment = allocator.makeEnvironment();
	environment.logger = log.makeEnvironment(glirps::LogSeverity::warning).logger;

	// ACT
	const glirps::CreateResult created
		= glirps::synthCreate(test_chip_instances_environment, test_voices_environment, &environment);

	// ASSERT
	EXPECT_FALSE(created);
	EXPECT_THAT(log.records, testing::ElementsAre(isRecord(glirps::LogSeverity::error, "allocation failed")));
}

TEST(EnvironmentTests, SynthCreate_PartialAllocator_LogsWarningAndUsesDefaultAllocation)
{
	// ARRANGE
	TrackingAllocator allocator;
	RecordingLog log;
	glirps::Environment environment = allocator.makeEnvironment();
	environment.allocator.deallocate = nullptr;
	environment.logger = log.makeEnvironment(glirps::LogSeverity::warning).logger;

	// ACT: a debug build asserts on a partial pair, so only a release build reaches the fallback.
#ifdef NDEBUG
	const glirps::SynthOwner synth(createSynthEnvironment(&environment));
	EXPECT_EQ(allocator.allocate_calls, 0U);
#else
	GTEST_SKIP() << "A partial AllocatorCallbacks asserts in debug builds.";
#endif

	// ASSERT
	EXPECT_THAT(log.records, testing::ElementsAre(isRecord(glirps::LogSeverity::warning, "partially populated")));
}

TEST(EnvironmentTests, SynthStart_PoolExhausted_LogsDebugNamingTheError)
{
	// ARRANGE
	RecordingLog log;
	const glirps::Environment environment = log.makeEnvironment(glirps::LogSeverity::debug);
	glirps::test_support::TestProgramStorage storage;
	const glirps::SoundProgram program = makeProgramEnvironment(storage);
	const glirps::SynthOwner synth(createSynthWithPoolsEnvironment(1, test_voices_environment, &environment));
	glirps::synthConfigure(synth.get(), test_sample_rate_hz_environment, test_oversampling_factor_environment);
	EXPECT_TRUE(glirps::synthStart(synth.get(), program, 0));
	log.records.clear();

	// ACT
	const glirps::StartResult second = glirps::synthStart(synth.get(), program, 0);

	// ASSERT
	EXPECT_FALSE(second);
	EXPECT_THAT(log.records, testing::ElementsAre(isRecord(glirps::LogSeverity::debug, "chip_instances_exhausted")));
}

TEST(EnvironmentTests, SynthStart_PoolExhausted_LogsNothingAtDefaultSeverity)
{
	// ARRANGE
	RecordingLog log;
	const glirps::Environment environment = log.makeEnvironment(glirps::LogSeverity::warning);
	glirps::test_support::TestProgramStorage storage;
	const glirps::SoundProgram program = makeProgramEnvironment(storage);
	const glirps::SynthOwner synth(createSynthWithPoolsEnvironment(1, test_voices_environment, &environment));
	glirps::synthConfigure(synth.get(), test_sample_rate_hz_environment, test_oversampling_factor_environment);
	EXPECT_TRUE(glirps::synthStart(synth.get(), program, 0));

	// ACT
	const glirps::StartResult second = glirps::synthStart(synth.get(), program, 0);

	// ASSERT
	EXPECT_FALSE(second);
	EXPECT_THAT(log.records, testing::IsEmpty());
}

TEST(EnvironmentTests, SynthStart_InvalidProgram_LogsWarningNamingTheError)
{
	// ARRANGE
	RecordingLog log;
	const glirps::Environment environment = log.makeEnvironment(glirps::LogSeverity::warning);
	const glirps::SynthOwner synth(createSynthEnvironment(&environment));
	glirps::synthConfigure(synth.get(), test_sample_rate_hz_environment, test_oversampling_factor_environment);
	const glirps::SoundProgram empty_program{};

	// ACT
	const glirps::StartResult started = glirps::synthStart(synth.get(), empty_program, 0);

	// ASSERT
	EXPECT_FALSE(started);
	EXPECT_EQ(started.error, glirps::StartError::invalid_program);
	EXPECT_THAT(log.records, testing::ElementsAre(isRecord(glirps::LogSeverity::warning, "invalid_program")));
}

TEST(EnvironmentTests, SynthLifecycle_WarningMinSeverity_SuppressesDebugMessages)
{
	// ARRANGE
	RecordingLog log;
	const glirps::Environment environment = log.makeEnvironment(glirps::LogSeverity::warning);

	// ACT
	{
		const glirps::SynthOwner synth(createSynthEnvironment(&environment));
		glirps::synthConfigure(synth.get(), test_sample_rate_hz_environment, test_oversampling_factor_environment);
	}

	// ASSERT
	EXPECT_THAT(log.records, testing::IsEmpty());
}

TEST(EnvironmentTests, SynthLifecycle_DebugMinSeverity_LogsCreateConfigureAndDestroy)
{
	// ARRANGE
	RecordingLog log;
	const glirps::Environment environment = log.makeEnvironment(glirps::LogSeverity::debug);

	// ACT
	{
		const glirps::SynthOwner synth(createSynthEnvironment(&environment));
		glirps::synthConfigure(synth.get(), test_sample_rate_hz_environment, test_oversampling_factor_environment);
	}

	// ASSERT
	EXPECT_THAT(log.records,
		testing::ElementsAre(isRecord(glirps::LogSeverity::debug, "synthCreate"),
			isRecord(glirps::LogSeverity::debug, "synthConfigure"),
			isRecord(glirps::LogSeverity::debug, "synthDestroy")));
}

TEST(EnvironmentTests, BlockRendererLifecycle_DebugMinSeverity_LogsCreateReconfigureAndDestroy)
{
	// ARRANGE
	RecordingLog log;
	const glirps::Environment environment = log.makeEnvironment(glirps::LogSeverity::debug);

	// ACT
	const glirps::BlockRendererCreateResult created = glirps::blockRendererCreate(test_sample_rate_hz_environment,
		test_oversampling_factor_environment,
		&environment);
	EXPECT_TRUE(created);
	glirps::blockRendererReconfigure(created.renderer,
		test_sample_rate_hz_environment,
		test_oversampling_factor_environment);
	glirps::blockRendererDestroy(created.renderer);

	// ASSERT
	EXPECT_THAT(log.records,
		testing::ElementsAre(isRecord(glirps::LogSeverity::debug, "blockRendererCreate"),
			isRecord(glirps::LogSeverity::debug, "blockRendererReconfigure"),
			isRecord(glirps::LogSeverity::debug, "blockRendererDestroy")));
}

TEST(EnvironmentTests, SynthRender_DebugMinSeverity_NeverLogs)
{
	// ARRANGE
	RecordingLog log;
	const glirps::Environment environment = log.makeEnvironment(glirps::LogSeverity::debug);
	glirps::test_support::TestProgramStorage storage;
	const glirps::SoundProgram program = makeProgramEnvironment(storage);
	const glirps::SynthOwner synth(createSynthEnvironment(&environment));
	glirps::synthConfigure(synth.get(), test_sample_rate_hz_environment, test_oversampling_factor_environment);
	const glirps::StartResult started = glirps::synthStart(synth.get(), program, 0);
	EXPECT_TRUE(started);
	std::vector<float> output(glirps::sub_block_frame_count * render_block_count_environment, 0.0f);
	log.records.clear();

	// ACT
	glirps::synthRender(synth.get(), glirps::shims::toSpan(output));
	glirps::synthSetGate(synth.get(), started.instance, false);
	glirps::synthApplyBlock(synth.get(), glirps::ParameterBlock{});

	// ASSERT
	EXPECT_THAT(log.records, testing::IsEmpty());
}
