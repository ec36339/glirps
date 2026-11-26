#include "impl/Synth.hpp"

#include "glirps/StartError.hpp"
#include "glirps/constants.hpp"
#include "glirps/frequency.hpp"

#include "SoundProgramTestSupport.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace
{
	using glirps::InterpolationMode;
	using glirps::Segment;
	using glirps::ValueRange;
	using glirps::test_support::makeSegmentTable;
	using glirps::test_support::TestProgramStorage;

	constexpr std::uint32_t test_sample_rate_hz = 48000;
	// One base tick, in subticks.
	constexpr std::uint32_t base_tick = glirps::subticks_per_base_tick;
	// The base voice gain of `makeProgram`, distinct from every table value
	// these tests author, so a block at the base value is recognizable.
	constexpr float base_gain = 0.5f;
	constexpr float tolerance = 1.0e-5f;

	// One sustained triangle voice at C-3 with the base gain above and the
	// given tick multiplier.
	glirps::SoundProgram& makeProgram(TestProgramStorage& storage, std::uint32_t tick_multiplier = 1)
	{
		storage.program.base_pitch = glirps::Pitch{.semitone = 36, .cents = 0};
		storage.program.grit.tick_multiplier = tick_multiplier;
		glirps::VoiceProgram voice;
		voice.waveform = glirps::Waveform::triangle;
		voice.gain = base_gain;
		glirps::test_support::setVoices(storage, {voice});
		return storage.program;
	}

	// The voice of `makeProgram`, for adding tables to.
	glirps::VoiceTables& editVoiceTables(TestProgramStorage& storage) { return storage.voice_storage.front().tables; }

	// A fixed table value.
	ValueRange<float> makeFixed(float value) { return ValueRange<float>{.min = value, .max = value}; }

	// Starts `program` with `seed` and returns every block voice 0 emits over
	// `base_ticks` base ticks, read through `read`. Empty if `start` fails.
	template<std::invocable<const glirps::ParameterBlock&> Read>
	std::vector<float>
	runSeeded(const glirps::SoundProgram& program, std::uint32_t base_ticks, std::uint64_t seed, const Read& read)
	{
		glirps::impl::Synth synth(1, glirps::max_voices_per_instance);
		synth.configure(test_sample_rate_hz);
		synth.setGritScale(0.0f);
		auto handle = synth.start(program, seed);
		if (!handle)
			return {};
		std::vector<float> values;
		std::vector<glirps::ParameterBlock> blocks(program.grit.tick_multiplier);
		for (std::uint32_t tick = 0; tick < base_ticks; ++tick)
		{
			const std::uint32_t count = synth.evaluateTick(*handle, blocks);
			for (const glirps::ParameterBlock& block : std::span(blocks).first(count))
				values.push_back(read(block));
		}
		return values;
	}

	// `runSeeded` with seed 0.
	template<std::invocable<const glirps::ParameterBlock&> Read>
	std::vector<float> run(const glirps::SoundProgram& program, std::uint32_t base_ticks, const Read& read)
	{
		return runSeeded(program, base_ticks, 0, read);
	}

	float readGain(const glirps::ParameterBlock& block) { return block.voices.front().gain; }

	float readGate(const glirps::ParameterBlock& block) { return block.voices.front().gate_on ? 1.0f : 0.0f; }

	// The error `start` reports for `program`, or `std::nullopt` if it
	// accepts it.
	std::optional<glirps::StartError> startError(const glirps::SoundProgram& program)
	{
		glirps::impl::Synth synth(1, glirps::max_voices_per_instance);
		synth.configure(test_sample_rate_hz);
		const auto handle = synth.start(program, 0);
		if (handle)
			return std::nullopt;
		return handle.error();
	}

	// A one-segment gain table of `values` at `interval_subticks` in `mode`.
	glirps::Table<float> makeGainSegment(TestProgramStorage& storage,
		std::vector<ValueRange<float>> values,
		std::uint16_t interval_subticks,
		InterpolationMode mode,
		std::optional<std::uint32_t> loop_start_subtick = std::nullopt)
	{
		const Segment segment{.start_subtick = 0,
			.first_value = 0,
			.value_count = static_cast<std::uint16_t>(values.size()),
			.interval_subticks = interval_subticks,
			.interpolation_mode = mode};
		return makeSegmentTable<float>(storage, {segment}, std::move(values), loop_start_subtick);
	}
}

// Interpolation modes

TEST(SynthSegmentTableTests, EvaluateTick_StepSegment_HoldsEachValueForItsInterval)
{
	// ARRANGE
	TestProgramStorage storage;
	const auto& program = makeProgram(storage);
	editVoiceTables(storage).has_gain = true;
	editVoiceTables(storage).gain
		= makeGainSegment(storage, {makeFixed(0.2f), makeFixed(0.6f)}, 2 * base_tick, InterpolationMode::step);

	// ACT
	const auto gains = run(program, 6, readGain);

	// ASSERT
	ASSERT_EQ(gains.size(), 6U);
	EXPECT_THAT(gains, testing::ElementsAre(0.2f, 0.2f, 0.6f, 0.6f, 0.6f, 0.6f));
}

TEST(SynthSegmentTableTests, EvaluateTick_LinearSegment_InterpolatesBetweenValues)
{
	// ARRANGE
	TestProgramStorage storage;
	const auto& program = makeProgram(storage);
	editVoiceTables(storage).has_gain = true;
	editVoiceTables(storage).gain
		= makeGainSegment(storage, {makeFixed(0.0f), makeFixed(1.0f)}, 4 * base_tick, InterpolationMode::linear);

	// ACT
	const auto gains = run(program, 6, readGain);

	// ASSERT
	ASSERT_EQ(gains.size(), 6U);
	EXPECT_THAT(gains,
		testing::Pointwise(testing::FloatNear(tolerance), std::vector<float>{0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.0f}));
}

TEST(SynthSegmentTableTests, EvaluateTick_CatmullRomSegment_FollowsSpline)
{
	// ARRANGE
	TestProgramStorage storage;
	const auto& program = makeProgram(storage);
	editVoiceTables(storage).has_gain = true;
	editVoiceTables(storage).gain = makeGainSegment(storage,
		{makeFixed(0.0f), makeFixed(1.0f), makeFixed(0.0f)},
		2 * base_tick,
		InterpolationMode::catmull_rom);

	// ACT
	const auto gains = run(program, 5, readGain);

	// ASSERT: at the half-interval points the outer neighbor repeats the
	// segment's own edge value, giving 0.5625 on both sides of the peak.
	ASSERT_EQ(gains.size(), 5U);
	EXPECT_THAT(gains,
		testing::Pointwise(testing::FloatNear(tolerance), std::vector<float>{0.0f, 0.5625f, 1.0f, 0.5625f, 0.0f}));
}

TEST(SynthSegmentTableTests, EvaluateTick_CatmullRomEdge_RepeatsSegmentEnd)
{
	// ARRANGE: a two-value spline followed by a far-off segment. Without
	// edge repetition the spline would read the next segment's 1.0.
	TestProgramStorage storage;
	const auto& program = makeProgram(storage);
	editVoiceTables(storage).has_gain = true;
	editVoiceTables(storage).gain = makeSegmentTable<float>(storage,
		{Segment{.start_subtick = 0,
			 .first_value = 0,
			 .value_count = 2,
			 .interval_subticks = 2 * base_tick,
			 .interpolation_mode = InterpolationMode::catmull_rom},
			Segment{.start_subtick = 4 * base_tick,
				.first_value = 2,
				.value_count = 1,
				.interval_subticks = base_tick,
				.interpolation_mode = InterpolationMode::step}},
		{makeFixed(0.0f), makeFixed(0.8f), makeFixed(1.0f)});

	// ACT
	const auto gains = run(program, 2, readGain);

	// ASSERT: p0 = p1 = 0 and p2 = p3 = 0.8 give exactly the midpoint.
	ASSERT_EQ(gains.size(), 2U);
	EXPECT_THAT(gains, testing::Pointwise(testing::FloatNear(tolerance), std::vector<float>{0.0f, 0.4f}));
}

// Time model

TEST(SynthSegmentTableTests, EvaluateTick_NonPowerOfTwoMultiplier_ExactBlockTimes)
{
	// ARRANGE: at a multiplier of 3, block 3 sits at exactly one base tick.
	// The second segment starts there, and the release time is there too.
	TestProgramStorage storage;
	auto& program = makeProgram(storage, 3);
	program.gate.has_release_subtick = true;
	program.gate.release_subtick = base_tick;
	editVoiceTables(storage).has_gain = true;
	editVoiceTables(storage).gain = makeSegmentTable<float>(storage,
		{Segment{.start_subtick = 0, .first_value = 0, .value_count = 1, .interval_subticks = base_tick},
			Segment{.start_subtick = base_tick, .first_value = 1, .value_count = 1, .interval_subticks = base_tick}},
		{makeFixed(0.25f), makeFixed(0.75f)});

	// ACT
	const auto gains = run(program, 2, readGain);
	const auto gates = run(program, 2, readGate);

	// ASSERT
	ASSERT_EQ(gains.size(), 6U);
	EXPECT_THAT(gains, testing::ElementsAre(0.25f, 0.25f, 0.25f, 0.75f, 0.75f, 0.75f));
	ASSERT_EQ(gates.size(), 6U);
	EXPECT_THAT(gates, testing::ElementsAre(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f));
}

TEST(SynthSegmentTableTests, EvaluateTick_GapsAroundSegments_HoldBaseThenLastValue)
{
	// ARRANGE: the first segment starts at 1 base tick and ends at 2; the
	// second starts at 4.
	TestProgramStorage storage;
	const auto& program = makeProgram(storage);
	editVoiceTables(storage).has_gain = true;
	editVoiceTables(storage).gain = makeSegmentTable<float>(storage,
		{Segment{.start_subtick = base_tick, .first_value = 0, .value_count = 1, .interval_subticks = base_tick},
			Segment{.start_subtick = 4 * base_tick,
				.first_value = 1,
				.value_count = 1,
				.interval_subticks = base_tick}},
		{makeFixed(0.2f), makeFixed(0.8f)});

	// ACT
	const auto gains = run(program, 6, readGain);

	// ASSERT
	ASSERT_EQ(gains.size(), 6U);
	EXPECT_THAT(gains, testing::ElementsAre(base_gain, 0.2f, 0.2f, 0.2f, 0.8f, 0.8f));
}

TEST(SynthSegmentTableTests, EvaluateTick_LoopBeforeFirstSegment_ReturnsToBase)
{
	// ARRANGE: one value from 1 base tick to 2, looping to time 0, which is
	// before the first segment.
	TestProgramStorage storage;
	const auto& program = makeProgram(storage);
	editVoiceTables(storage).has_gain = true;
	editVoiceTables(storage).gain = makeSegmentTable<float>(storage,
		{Segment{.start_subtick = base_tick, .first_value = 0, .value_count = 1, .interval_subticks = base_tick}},
		{makeFixed(0.2f)},
		0);

	// ACT
	const auto gains = run(program, 6, readGain);

	// ASSERT
	ASSERT_EQ(gains.size(), 6U);
	EXPECT_THAT(gains, testing::ElementsAre(base_gain, 0.2f, base_gain, 0.2f, base_gain, 0.2f));
}

TEST(SynthSegmentTableTests, EvaluateTick_LoopStartInsideSegment_WrapsByTime)
{
	// ARRANGE: four one-tick values, end 64, looping to 24, half way through
	// the second value, so the loop period is 40 subticks.
	TestProgramStorage storage;
	const auto& program = makeProgram(storage);
	editVoiceTables(storage).has_gain = true;
	editVoiceTables(storage).gain = makeGainSegment(storage,
		{makeFixed(0.1f), makeFixed(0.2f), makeFixed(0.3f), makeFixed(0.4f)},
		base_tick,
		InterpolationMode::step,
		24);

	// ACT
	const auto gains = run(program, 9, readGain);

	// ASSERT: times 64, 80, 96, 112, 128 wrap to 24, 40, 56, 32, 48.
	ASSERT_EQ(gains.size(), 9U);
	EXPECT_THAT(gains, testing::ElementsAre(0.1f, 0.2f, 0.3f, 0.4f, 0.2f, 0.3f, 0.4f, 0.3f, 0.4f));
}

TEST(SynthSegmentTableTests, EvaluateTick_LongRunningLoop_StaysOnPattern)
{
	// ARRANGE
	constexpr std::uint32_t loop_start = 24;
	constexpr std::uint32_t value_count = 4;
	constexpr std::uint64_t loop_period = (value_count * base_tick) - loop_start;
	TestProgramStorage storage;
	const auto& program = makeProgram(storage);
	editVoiceTables(storage).has_gain = true;
	editVoiceTables(storage).gain = makeGainSegment(storage,
		{makeFixed(0.1f), makeFixed(0.2f), makeFixed(0.3f), makeFixed(0.4f)},
		base_tick,
		InterpolationMode::step,
		loop_start);
	constexpr std::uint32_t base_ticks = 100000;

	// ACT
	const auto gains = run(program, base_ticks, readGain);

	// ASSERT: the last block, at time (base_ticks - 1) * 16, matches the loop
	// wrap `loop_start + (t - loop_start) mod (end - loop_start)`.
	constexpr std::uint64_t time = (base_ticks - 1) * std::uint64_t{base_tick};
	constexpr std::uint64_t wrapped = loop_start + ((time - loop_start) % loop_period);
	const std::vector<float> by_value{0.1f, 0.2f, 0.3f, 0.4f};
	ASSERT_EQ(gains.size(), base_ticks);
	EXPECT_EQ(gains.back(), by_value.at(wrapped / base_tick));
}

// Random values

TEST(SynthSegmentTableTests, EvaluateTick_RandomValue_RedrawnEachPass)
{
	// ARRANGE: a random value held for two ticks, then a fixed one, looping.
	TestProgramStorage storage;
	const auto& program = makeProgram(storage);
	editVoiceTables(storage).has_gain = true;
	editVoiceTables(storage).gain = makeGainSegment(storage,
		{ValueRange<float>{.min = 0.0f, .max = 1.0f}, makeFixed(0.5f)},
		2 * base_tick,
		InterpolationMode::step,
		0);

	// ACT
	const auto gains = runSeeded(program, 8, 1234, readGain);

	// ASSERT: the same draw for both ticks of a pass, a new one each pass.
	ASSERT_EQ(gains.size(), 8U);
	EXPECT_EQ(gains.at(0), gains.at(1));
	EXPECT_EQ(gains.at(4), gains.at(5));
	EXPECT_NE(gains.at(0), gains.at(4));
	EXPECT_EQ(gains.at(2), 0.5f);
}

TEST(SynthSegmentTableTests, EvaluateTick_RandomNeighbours_ShareDraw)
{
	// ARRANGE: a linear segment between two random values, not looping.
	TestProgramStorage storage;
	const auto& program = makeProgram(storage);
	editVoiceTables(storage).has_gain = true;
	editVoiceTables(storage).gain = makeGainSegment(storage,
		{ValueRange<float>{.min = 0.0f, .max = 1.0f}, ValueRange<float>{.min = 0.0f, .max = 1.0f}},
		4 * base_tick,
		InterpolationMode::linear);

	// ACT
	const auto gains = runSeeded(program, 6, 99, readGain);

	// ASSERT: the midpoint uses the same two draws as the endpoints, and
	// the end value holds without a redraw.
	ASSERT_EQ(gains.size(), 6U);
	EXPECT_NEAR(gains.at(2), (gains.at(0) + gains.at(4)) * 0.5f, tolerance);
	EXPECT_EQ(gains.at(4), gains.at(5));
}

TEST(SynthSegmentTableTests, EvaluateTick_RandomValue_DeterministicPerSeed)
{
	// ARRANGE
	TestProgramStorage storage;
	const auto& program = makeProgram(storage);
	editVoiceTables(storage).has_gain = true;
	editVoiceTables(storage).gain = makeGainSegment(storage,
		{ValueRange<float>{.min = 0.0f, .max = 1.0f}},
		base_tick,
		InterpolationMode::step,
		0);

	// ACT
	const auto first = runSeeded(program, 16, 7, readGain);
	const auto again = runSeeded(program, 16, 7, readGain);
	const auto other = runSeeded(program, 16, 8, readGain);

	// ASSERT
	EXPECT_EQ(first, again);
	EXPECT_NE(first, other);
}

TEST(SynthSegmentTableTests, EvaluateTick_PitchOffset_AddsFractionalSemitones)
{
	// ARRANGE
	TestProgramStorage storage;
	const auto& program = makeProgram(storage);
	editVoiceTables(storage).has_pitch_offset = true;
	editVoiceTables(storage).pitch_offset = makeSegmentTable<float>(storage,
		{Segment{.start_subtick = 0, .first_value = 0, .value_count = 2, .interval_subticks = base_tick}},
		{makeFixed(12.0f), makeFixed(0.5f)});

	// ACT
	const auto frequencies
		= run(program, 2, [](const glirps::ParameterBlock& block) { return block.voices.front().frequency_hz; });

	// ASSERT
	ASSERT_EQ(frequencies.size(), 2U);
	EXPECT_THAT(frequencies,
		testing::ElementsAre(testing::FloatEq(glirps::frequencyHertz(48, 0.0f)),
			testing::FloatEq(glirps::frequencyHertz(36, 50.0f))));
}

TEST(SynthSegmentTableTests, EvaluateTick_PulseWidth_ReplacesBase)
{
	// ARRANGE
	TestProgramStorage storage;
	const auto& program = makeProgram(storage);
	editVoiceTables(storage).has_pulse_width = true;
	editVoiceTables(storage).pulse_width
		= makeGainSegment(storage, {makeFixed(0.25f), makeFixed(1.5f)}, base_tick, InterpolationMode::step);

	// ACT
	const auto widths
		= run(program, 2, [](const glirps::ParameterBlock& block) { return block.voices.front().pulse_width; });

	// ASSERT: the second value clamps to the duty range.
	ASSERT_EQ(widths.size(), 2U);
	EXPECT_THAT(widths, testing::ElementsAre(0.25f, 1.0f));
}

TEST(SynthSegmentTableTests, EvaluateTick_PitchSlide_AccumulatesOncePerControlTick)
{
	// ARRANGE: a multiplier of 2 emits two effective ticks per base tick.
	TestProgramStorage storage;
	const auto& program = makeProgram(storage, 2);
	editVoiceTables(storage).has_pitch_slide = true;
	editVoiceTables(storage).pitch_slide
		= makeSegmentTable<float>(storage, {Segment{.interval_subticks = base_tick}}, {makeFixed(10.0f)});

	// ACT
	const auto frequencies
		= run(program, 2, [](const glirps::ParameterBlock& block) { return block.voices.front().frequency_hz; });

	// ASSERT
	ASSERT_EQ(frequencies.size(), 4U);
	EXPECT_THAT(frequencies,
		testing::ElementsAre(testing::FloatEq(glirps::frequencyHertz(36, 10.0f)),
			testing::FloatEq(glirps::frequencyHertz(36, 20.0f)),
			testing::FloatEq(glirps::frequencyHertz(36, 30.0f)),
			testing::FloatEq(glirps::frequencyHertz(36, 40.0f))));
}

TEST(SynthSegmentTableTests, EvaluateTick_StartSubtick_ActivatesVoiceAndTables)
{
	// ARRANGE: at a multiplier of 2, half a base tick is block 1.
	TestProgramStorage storage;
	const auto& program = makeProgram(storage, 2);
	storage.voice_storage.front().start_subtick = base_tick / 2;
	editVoiceTables(storage).has_gain = true;
	editVoiceTables(storage).gain
		= makeGainSegment(storage, {makeFixed(0.2f), makeFixed(0.6f)}, base_tick / 2, InterpolationMode::step);

	// ACT
	const auto gains = run(program, 2, readGain);
	const auto gates = run(program, 2, readGate);

	// ASSERT
	ASSERT_EQ(gates.size(), 4U);
	EXPECT_THAT(gates, testing::ElementsAre(0.0f, 1.0f, 1.0f, 1.0f));
	ASSERT_EQ(gains.size(), 4U);
	EXPECT_THAT(gains, testing::ElementsAre(base_gain, 0.2f, 0.6f, 0.6f));
}

// `start` validation

TEST(SynthSegmentTableTests, Start_MalformedTables_RejectedAsInvalidProgram)
{
	// ARRANGE: each case builds one malformed table into a fresh program.
	const Segment one_tick{.start_subtick = 0, .first_value = 0, .value_count = 1, .interval_subticks = base_tick};
	constexpr auto out_of_range_mode = static_cast<InterpolationMode>(7);
	const auto two_values = [&](InterpolationMode mode, std::uint16_t interval)
	{
		return Segment{.start_subtick = 0,
			.first_value = 0,
			.value_count = 2,
			.interval_subticks = interval,
			.interpolation_mode = mode};
	};
	const std::vector<std::function<void(TestProgramStorage&)>> cases{
		[](TestProgramStorage& case_storage) { editVoiceTables(case_storage).has_gain = true; },
		[&](TestProgramStorage& case_storage)
		{
			editVoiceTables(case_storage).has_gain = true;
			editVoiceTables(case_storage).gain = makeSegmentTable<float>(case_storage,
				{Segment{.start_subtick = 0, .first_value = 0, .value_count = 1, .interval_subticks = 0}},
				{makeFixed(0.5f)});
		},
		[&](TestProgramStorage& case_storage)
		{
			editVoiceTables(case_storage).has_gain = true;
			editVoiceTables(case_storage).gain = makeSegmentTable<float>(case_storage,
				{two_values(InterpolationMode::step, base_tick)},
				{makeFixed(0.5f)});
		},
		[&](TestProgramStorage& case_storage)
		{
			editVoiceTables(case_storage).has_gain = true;
			editVoiceTables(case_storage).gain = makeSegmentTable<float>(case_storage,
				{two_values(InterpolationMode::step, base_tick),
					Segment{.start_subtick = base_tick,
						.first_value = 0,
						.value_count = 1,
						.interval_subticks = base_tick}},
				{makeFixed(0.5f), makeFixed(0.6f)});
		},
		[&](TestProgramStorage& case_storage)
		{
			editVoiceTables(case_storage).has_gain = true;
			editVoiceTables(case_storage).gain
				= makeSegmentTable<float>(case_storage, {one_tick, one_tick}, {makeFixed(0.5f)});
		},
		[&](TestProgramStorage& case_storage)
		{
			editVoiceTables(case_storage).has_gain = true;
			editVoiceTables(case_storage).gain
				= makeSegmentTable<float>(case_storage, {one_tick}, {makeFixed(0.5f)}, base_tick);
		},
		[&](TestProgramStorage& case_storage)
		{
			editVoiceTables(case_storage).has_arpeggio = true;
			editVoiceTables(case_storage).arpeggio = makeSegmentTable<std::int32_t>(case_storage,
				{two_values(InterpolationMode::linear, base_tick)},
				{ValueRange<std::int32_t>{.min = 0, .max = 0}, ValueRange<std::int32_t>{.min = 1, .max = 1}});
		},
		[&](TestProgramStorage& case_storage)
		{
			editVoiceTables(case_storage).has_gain = true;
			editVoiceTables(case_storage).gain = makeSegmentTable<float>(case_storage,
				{two_values(InterpolationMode::step, base_tick / 2)},
				{makeFixed(0.5f), makeFixed(0.6f)});
		},
		[&](TestProgramStorage& case_storage)
		{
			editVoiceTables(case_storage).has_gain = true;
			editVoiceTables(case_storage).gain = makeSegmentTable<float>(case_storage,
				{two_values(out_of_range_mode, base_tick)},
				{makeFixed(0.5f), makeFixed(0.6f)});
		},
		[&](TestProgramStorage& case_storage)
		{
			editVoiceTables(case_storage).has_gain = true;
			editVoiceTables(case_storage).gain = makeSegmentTable<float>(case_storage,
				{one_tick},
				{ValueRange<float>{.min = 0.0f, .max = std::numeric_limits<float>::infinity()}});
		},
		[&](TestProgramStorage& case_storage)
		{
			editVoiceTables(case_storage).has_gain = true;
			editVoiceTables(case_storage).gain
				= makeSegmentTable<float>(case_storage, {one_tick}, {ValueRange<float>{.min = 0.8f, .max = 0.2f}});
		},
		[&](TestProgramStorage& case_storage)
		{
			editVoiceTables(case_storage).has_pulse_width = true;
			editVoiceTables(case_storage).pulse_width
				= makeSegmentTable<float>(case_storage, {one_tick}, {makeFixed(0.5f)});
			editVoiceTables(case_storage).has_pulse_width_delta = true;
			editVoiceTables(case_storage).pulse_width_delta
				= makeSegmentTable<float>(case_storage, {one_tick}, {makeFixed(0.0f)});
		},
	};

	// ACT
	std::vector<std::optional<glirps::StartError>> errors;
	for (const auto& make_malformed : cases)
	{
		TestProgramStorage storage;
		const auto& program = makeProgram(storage);
		make_malformed(storage);
		errors.push_back(startError(program));
	}

	// ASSERT
	ASSERT_EQ(errors.size(), cases.size());
	EXPECT_THAT(errors, testing::Each(testing::Optional(glirps::StartError::invalid_program)));
}

TEST(SynthSegmentTableTests, Start_OneControlTickInterval_Accepted)
{
	// ARRANGE: half a base tick is one effective tick at a multiplier of 2,
	// and a one-value segment may be shorter than an effective tick.
	TestProgramStorage two_values_storage;
	const auto& two_values = makeProgram(two_values_storage, 2);
	editVoiceTables(two_values_storage).has_gain = true;
	editVoiceTables(two_values_storage).gain = makeGainSegment(two_values_storage,
		{makeFixed(0.2f), makeFixed(0.6f)},
		base_tick / 2,
		InterpolationMode::step);
	TestProgramStorage one_value_storage;
	const auto& one_value = makeProgram(one_value_storage);
	editVoiceTables(one_value_storage).has_gain = true;
	editVoiceTables(one_value_storage).gain
		= makeGainSegment(one_value_storage, {makeFixed(0.2f)}, 1, InterpolationMode::step);

	// ACT
	const auto two_values_error = startError(two_values);
	const auto one_value_error = startError(one_value);

	// ASSERT
	EXPECT_EQ(two_values_error, std::nullopt);
	EXPECT_EQ(one_value_error, std::nullopt);
}
