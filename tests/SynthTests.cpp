#include "impl/Synth.hpp"

#include "glirps/GritProfile.hpp"
#include "glirps/constants.hpp"
#include "glirps/frequency.hpp"
#include "impl/BlockRenderer.hpp"
#include "impl/DenormalGuard.hpp"
#include "impl/Mixer.hpp"

#include "SoundProgramTestSupport.hpp"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include <pmmintrin.h>
#include <xmmintrin.h>
#endif

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include <pmmintrin.h>
#include <xmmintrin.h>
#endif

namespace
{
	constexpr std::uint32_t test_sample_rate_hz_synth = 48000;

	// Renders `program` through a fresh 8-by-32 `Synth` and returns whether it
	// started.
	bool renderThroughSynth(const glirps::SoundProgram& program, std::uint32_t frame_count, std::vector<float>& output)
	{
		glirps::impl::Synth synth(8, 32);
		synth.configure(test_sample_rate_hz_synth);
		auto handle = synth.start(program, 0);
		if (!handle)
			return false;
		output.assign(frame_count, 0.0f);
		synth.render(output);
		return true;
	}

	float peakOf(const std::vector<float>& samples)
	{
		float peak = 0.0f;
		for (const float sample : samples)
			peak = std::max(peak, std::fabs(sample));
		return peak;
	}

	// A gmock `Mixer` whose interactions the tests drive through
	// `ON_CALL`/`EXPECT_CALL`. The recording behavior the test needs is installed
	// per test via `Invoke`; the class carries the observed state so the test can
	// assert it.
	class MockMixer : public glirps::impl::Mixer
	{
	public:
		MOCK_METHOD(std::uint32_t, getChannelCount, (), (const, noexcept, override));
		MOCK_METHOD(void, beginBlock, (std::span<float> output, std::uint32_t frame_count), (noexcept, override));
		MOCK_METHOD(void,
			submitInstance,
			(glirps::ChipInstanceHandle instance, std::span<const float> mono),
			(noexcept, override));
		MOCK_METHOD(void, endBlock, (), (noexcept, override));

		std::span<float> output;
		std::uint32_t frame_count = 0;
		std::uint32_t submissions_this_block = 0;
		std::uint32_t blocks_begun = 0;
		std::vector<std::uint32_t> per_block_submissions;
		std::vector<glirps::ChipInstanceHandle> handles;
		// FTZ/DAZ modes observed inside a `submitInstance` callback, so the
		// denormal-guard test can see them from within the render call.
		unsigned int flush_zero_mode = 0;
		unsigned int denormals_zero_mode = 0;
	};
}

// `nominal_source_level_db` survives a full-amplitude triangle at unity
// gains: it is the nominal source level every authored gain is relative to.
TEST(SynthTests, Render_UnityGains_PeaksAtNominalSourceLevel)
{
	// ARRANGE
	glirps::test_support::TestProgramStorage program_storage;
	glirps::SoundProgram& program = program_storage.program;
	program.base_pitch = glirps::Pitch{.semitone = 36, .cents = 0};
	glirps::VoiceProgram voice;
	voice.waveform = glirps::Waveform::triangle;
	voice.gain = 1.0f;
	voice.envelope = glirps::EnvelopeSpec{.attack_seconds = 0.002f,
		.decay_seconds = 0.006f,
		.sustain_level = 1.0f,
		.release_seconds = 0.006f};
	glirps::test_support::setVoices(program_storage, {voice});

	std::vector<float> output;
	ASSERT_TRUE(renderThroughSynth(program, 48000, output));
	const float peak = peakOf(output);

	// ACT
	const float to_dbfs = glirps::decibel_amplitude_scale * std::log10(peak / glirps::full_scale);

	// ASSERT
	EXPECT_NEAR(to_dbfs, glirps::nominal_source_level_db, 0.5f);
}

// `Synth::render` fills the caller's span with float samples and does not
// constrain them to `full_scale`: a program whose output gain exceeds unity
// renders a peak that exceeds full scale, since `glirps` applies no limiting.
TEST(SynthTests, Render_AboveUnityGain_DoesNotConstrainSamplesToFullScale)
{
	// ARRANGE
	glirps::test_support::TestProgramStorage program_storage;
	glirps::SoundProgram& program = program_storage.program;
	program.output_gain = 4.5f;
	glirps::test_support::resizeVoices(program_storage, 1);
	program_storage.voice_storage.at(0).waveform = glirps::Waveform::triangle;
	program_storage.voice_storage.at(0).gain = 1.0f;
	program_storage.voice_storage.at(0).envelope = glirps::EnvelopeSpec{.attack_seconds = 0.002f,
		.decay_seconds = 0.006f,
		.sustain_level = 1.0f,
		.release_seconds = 0.006f};

	std::vector<float> output;
	ASSERT_TRUE(renderThroughSynth(program, 48000, output));

	// ACT
	const float peak = peakOf(output);

	// ASSERT
	EXPECT_GT(peak, glirps::full_scale);
}

// `start` reports `chip_instances_exhausted` once every slot is taken, and a
// released slot can be claimed again.
TEST(SynthTests, Start_WithEveryChipSlotTaken_ReturnsChipInstancesExhausted)
{
	// ARRANGE
	glirps::impl::Synth synth(2, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	glirps::SoundProgram& program = program_storage.program;
	glirps::test_support::resizeVoices(program_storage, 1);

	// ACT
	auto first = synth.start(program, 0);
	ASSERT_TRUE(first.has_value());
	auto second = synth.start(program, 0);
	ASSERT_TRUE(second.has_value());
	auto third = synth.start(program, 0);
	ASSERT_FALSE(third.has_value());

	// ASSERT
	EXPECT_EQ(third.error(), glirps::StartError::chip_instances_exhausted);
	synth.release(*second);
	// A released slot is not reclaimable until `render` itself acknowledges
	// the release.
	std::vector<float> output(glirps::sub_block_frame_count, 0.0f);
	synth.render(output);
	auto recovered = synth.start(program, 0);
	ASSERT_TRUE(recovered.has_value());
}

// `start` reports `voices_exhausted` when a chip slot is free but the free
// voice count cannot cover the program, since voice stealing is not
// supported.
TEST(SynthTests, Start_WithFreeSlotButTightVoicePool_ReturnsVoicesExhausted)
{
	// ARRANGE
	glirps::impl::Synth synth(2, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage one_voice_storage;
	glirps::SoundProgram& one_voice = one_voice_storage.program;
	glirps::test_support::resizeVoices(one_voice_storage, 1);
	auto first = synth.start(one_voice, 0);
	ASSERT_TRUE(first.has_value());
	auto second = synth.start(one_voice, 0);
	ASSERT_TRUE(second.has_value());
	// Two voices remain free with both slots taken; free one slot so the voice
	// ceiling is the one that is short.
	synth.release(*first);
	std::vector<float> acknowledge_release(glirps::sub_block_frame_count, 0.0f);
	synth.render(acknowledge_release);
	glirps::test_support::TestProgramStorage four_voice_storage;
	glirps::SoundProgram& four_voice = four_voice_storage.program;
	glirps::test_support::resizeVoices(four_voice_storage, 4);

	// ACT
	auto voices = synth.start(four_voice, 0);
	ASSERT_FALSE(voices.has_value());

	// ASSERT
	EXPECT_EQ(voices.error(), glirps::StartError::voices_exhausted);
}

// `start` rejects invalid programs (no voices, too many voices, and a table
// with no values), and no failing case leaves a chip instance slot or a
// voice slot claimed: `start` never steals, queues, or partially allocates.
TEST(SynthTests, Start_InvalidPrograms_RejectedAndNoSlotClaimed)
{
	// ARRANGE
	glirps::impl::Synth synth(2, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	glirps::SoundProgram& program = program_storage.program;
	glirps::test_support::resizeVoices(program_storage, 1);
	glirps::test_support::TestProgramStorage empty_storage;
	glirps::SoundProgram& empty = empty_storage.program;
	glirps::test_support::TestProgramStorage too_many_storage;
	glirps::SoundProgram& too_many = too_many_storage.program;
	glirps::test_support::resizeVoices(too_many_storage, glirps::max_voices_per_instance + 1);
	glirps::test_support::TestProgramStorage empty_table_storage;
	glirps::SoundProgram& empty_table = empty_table_storage.program;
	glirps::test_support::resizeVoices(empty_table_storage, 1);
	empty_table_storage.voice_storage.at(0).tables.has_gain = true;
	empty_table_storage.voice_storage.at(0).tables.gain
		= glirps::test_support::makeFloatTable(empty_table_storage, {}, 0);

	// ACT & ASSERT
	auto invalid_empty = synth.start(empty, 0);
	ASSERT_FALSE(invalid_empty.has_value());
	EXPECT_EQ(invalid_empty.error(), glirps::StartError::invalid_program);

	auto invalid_too_many = synth.start(too_many, 0);
	ASSERT_FALSE(invalid_too_many.has_value());
	EXPECT_EQ(invalid_too_many.error(), glirps::StartError::invalid_program);

	auto invalid_empty_table = synth.start(empty_table, 0);
	ASSERT_FALSE(invalid_empty_table.has_value());
	EXPECT_EQ(invalid_empty_table.error(), glirps::StartError::invalid_program);

	// No failing case left a slot claimed: the single-voice program starts.
	auto recovered = synth.start(program, 0);
	ASSERT_TRUE(recovered.has_value());
}

// When the chip-instance pool is exhausted the error names that ceiling,
// even when the free voice count is also short: a `Synth(1,1)` holding one
// live instance asked for a two-voice program reports
// `chip_instances_exhausted`.
TEST(SynthTests, Start_WhenSlotAndVoicesBothExhausted_ReportsSlotCeiling)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 1);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage one_voice_storage;
	glirps::SoundProgram& one_voice = one_voice_storage.program;
	glirps::test_support::resizeVoices(one_voice_storage, 1);
	auto first = synth.start(one_voice, 0);
	ASSERT_TRUE(first.has_value());

	// ACT
	glirps::test_support::TestProgramStorage two_voice_storage;
	glirps::SoundProgram& two_voice = two_voice_storage.program;
	glirps::test_support::resizeVoices(two_voice_storage, 2);
	auto second = synth.start(two_voice, 0);

	// ASSERT
	ASSERT_FALSE(second.has_value());
	EXPECT_EQ(second.error(), glirps::StartError::chip_instances_exhausted);
}

// `start` stores a non-owning pointer to `program` rather than a copy, so it
// never allocates. Mutating the source program after `start` therefore changes
// what the NEXT `evaluateTick` observes: fields it reads straight from
// `*slot.program` on every call (`base_pitch`, here) reflect
// the caller's current object, not a snapshot frozen at `start` time. The
// caller's obligation is therefore that `program` outlives the instance, not
// merely the `start` call, which is why it is a named local held for the whole
// test rather than destroyed mid-test.
TEST(SynthTests, Start_StoresNonOwningPointer_SubsequentTickObservesSourceMutation)
{
	// ARRANGE
	glirps::test_support::TestProgramStorage program_storage;
	glirps::SoundProgram& program = program_storage.program;
	program.base_pitch = glirps::Pitch{.semitone = 36, .cents = 0};
	glirps::test_support::resizeVoices(program_storage, 1);
	program_storage.voice_storage.at(0).waveform = glirps::Waveform::triangle;

	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	std::array<glirps::ParameterBlock, 1> blocks;
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
	const float before_frequency = blocks.at(0).voices.at(0).frequency_hz;

	// ACT: mutate the source program, one octave up, after `start`.
	program.base_pitch.semitone += 12;
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
	const float after_frequency = blocks.at(0).voices.at(0).frequency_hz;

	// ASSERT: the next tick reflects the mutated pitch, one octave above the
	// pre-mutation tick, rather than the value `start` saw.
	EXPECT_NEAR(after_frequency, before_frequency * 2.0f, 0.1f);
}

// A released handle reports `isComplete` true, `release` on it is a no-op, and
// the newly issued handle is not equal to it.
TEST(SynthTests, IsComplete_ReleasedHandle_IsTrueAndReleaseNoOp)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	glirps::SoundProgram& program = program_storage.program;
	glirps::test_support::resizeVoices(program_storage, 1);

	auto first = synth.start(program, 0);
	ASSERT_TRUE(first.has_value());

	// ACT
	synth.release(*first);
	const bool complete_after_release = synth.isComplete(*first);
	synth.release(*first);

	std::vector<float> output(glirps::sub_block_frame_count, 0.0f);
	synth.render(output);
	auto second = synth.start(program, 0);
	ASSERT_TRUE(second.has_value());

	// ASSERT
	EXPECT_TRUE(complete_after_release);
	EXPECT_NE(*first, *second);
	// The stale handle stays complete (expired).
	EXPECT_TRUE(synth.isComplete(*first));
}

// A substitute `Mixer` receives one `submitInstance` per live chip instance per
// sub-block, each carrying the instance's handle so a spatial mixer can
// resolve its emitter.
TEST(SynthTests, Render_SubstituteMixer_ReceivesOneSubmitPerInstancePerSubBlock)
{
	// ARRANGE
	glirps::impl::Synth synth(8, 8);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	glirps::SoundProgram& program = program_storage.program;
	glirps::test_support::resizeVoices(program_storage, 1);

	auto first = synth.start(program, 0);
	auto second = synth.start(program, 0);
	ASSERT_TRUE(first.has_value());
	ASSERT_TRUE(second.has_value());

	MockMixer mixer;
	ON_CALL(mixer, getChannelCount()).WillByDefault(::testing::Return(1u));
	ON_CALL(mixer, beginBlock(::testing::_, ::testing::_))
		.WillByDefault(::testing::Invoke(
			[&](std::span<float> output, std::uint32_t frame_count)
			{
				mixer.output = output;
				mixer.frame_count = frame_count;
				mixer.submissions_this_block = 0;
				mixer.blocks_begun++;
			}));
	ON_CALL(mixer, submitInstance(::testing::_, ::testing::_))
		.WillByDefault(::testing::Invoke(
			[&](glirps::ChipInstanceHandle instance, std::span<const float> mono)
			{
				mixer.submissions_this_block++;
				mixer.handles.push_back(instance);
				std::ranges::transform(mixer.output, mono, mixer.output.begin(), std::plus<>{});
			}));
	ON_CALL(mixer, endBlock())
		.WillByDefault(
			::testing::Invoke([&]() { mixer.per_block_submissions.push_back(mixer.submissions_this_block); }));
	synth.setMixer(mixer);

	std::vector<float> output(128, 0.0f);

	// ACT
	synth.render(output);

	// ASSERT
	// 128 frames is two sub-blocks of `sub_block_frame_count`, two live
	// instances each.
	ASSERT_EQ(mixer.blocks_begun, 2u);
	ASSERT_EQ(mixer.per_block_submissions.size(), 2u);
	EXPECT_EQ(mixer.per_block_submissions.at(0), 2u);
	EXPECT_EQ(mixer.per_block_submissions.at(1), 2u);
	ASSERT_EQ(mixer.handles.size(), 4u);
	EXPECT_EQ(mixer.handles.at(0), *first);
	EXPECT_EQ(mixer.handles.at(1), *second);
	EXPECT_EQ(mixer.handles.at(2), *first);
	EXPECT_EQ(mixer.handles.at(3), *second);
	// The rendered output is not silence, so the mixer's writes landed.
	EXPECT_GT(peakOf(output), 0.0f);
}

// `render` overwrites the caller's span rather than accumulating into it.
TEST(SynthTests, Render_CallerSpanContents_AreOverwritten)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	glirps::SoundProgram& program = program_storage.program;
	glirps::test_support::resizeVoices(program_storage, 1);

	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	std::vector<float> output(128, 1.0f);
	synth.render(output);
	std::vector<float> first_render = output;

	// ACT: fill with a sentinel and render silence (release so nothing is live).
	synth.release(*handle);
	std::fill(output.begin(), output.end(), 1.0f);
	synth.render(output);

	// ASSERT
	// No live instance: the mixer writes zeroes, overwriting the sentinel.
	for (const float sample : output)
		EXPECT_EQ(sample, 0.0f);
	// The earlier render was not silence.
	EXPECT_GT(peakOf(first_render), 0.0f);
}

// The denormal guard enables both modes for the duration of a `render` call
// and restores the mode the caller had on exit. On non-x86 the FTZ/DAZ
// controls do not exist, so the test is skipped there rather than failed.
TEST(SynthTests, Render_DenormalGuard_EnablesFtZAndRestoresCallerMode)
{
#if !defined(_M_IX86) && !defined(_M_X64) && !defined(__i386__) && !defined(__x86_64__)
	GTEST_SKIP() << "FTZ/DAZ are x86 SSE controls";
#else
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	glirps::SoundProgram& program = program_storage.program;
	glirps::test_support::resizeVoices(program_storage, 1);
	ASSERT_TRUE(synth.start(program, 0).has_value());

	// Set the caller's mode to both-off before the call, so restoration is
	// observable.
	_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_OFF);
	_MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_OFF);

	MockMixer mixer;
	ON_CALL(mixer, getChannelCount()).WillByDefault(::testing::Return(1u));
	ON_CALL(mixer, submitInstance(::testing::_, ::testing::_))
		.WillByDefault(::testing::Invoke(
			[&](glirps::ChipInstanceHandle, std::span<const float>)
			{
				mixer.flush_zero_mode = _MM_GET_FLUSH_ZERO_MODE();
				mixer.denormals_zero_mode = _MM_GET_DENORMALS_ZERO_MODE();
			}));
	synth.setMixer(mixer);
	std::vector<float> output(64);

	// ACT
	synth.render(output);

	// ASSERT
	// Both modes were on for the duration of the render call (observed from
	// inside the mixer callback).
	EXPECT_EQ(mixer.flush_zero_mode, _MM_FLUSH_ZERO_ON);
	EXPECT_EQ(mixer.denormals_zero_mode, _MM_DENORMALS_ZERO_ON);
	// The caller's previous mode is restored on exit.
	EXPECT_EQ(_MM_GET_FLUSH_ZERO_MODE(), _MM_FLUSH_ZERO_OFF);
	EXPECT_EQ(_MM_GET_DENORMALS_ZERO_MODE(), _MM_DENORMALS_ZERO_OFF);
#endif
}

// `render`'s contract is a whole number of sub-blocks. In debug builds the
// assert fires; in release builds the trailing partial sub-block is zeroed so
// the whole span is still overwritten.
TEST(SynthTests, Render_NonWholeSubBlockCount_IsRejectedOrZeroFilled)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	glirps::SoundProgram& program = program_storage.program;
	glirps::test_support::resizeVoices(program_storage, 1);
	ASSERT_TRUE(synth.start(program, 0).has_value());

	std::vector<float> output(glirps::sub_block_frame_count - 1);

	// ACT & ASSERT
#if defined(NDEBUG)
	// Release: no assert, so the violated contract must still produce a
	// defined result: the partial sub-block is zeroed, not left stale.
	synth.render(output);
	for (const float sample : output)
		EXPECT_FLOAT_EQ(sample, 0.0f);
#else
	// Debug: the assert rejects the bad frame count.
	EXPECT_DEATH(synth.render(output), "");
#endif
}

// An instance started and then rendered with no block applied produces
// non-silent output shaped by its program's base values: an absent block
// leaves every parameter at its base value rather than silence.
TEST(SynthTests, Render_NoBlockApplied_RendersProgramBaseValues)
{
	// ARRANGE
	glirps::test_support::TestProgramStorage program_storage;
	glirps::SoundProgram& program = program_storage.program;
	program.base_pitch = glirps::Pitch{.semitone = 36, .cents = 0};
	glirps::VoiceProgram voice;
	voice.waveform = glirps::Waveform::triangle;
	voice.gain = 1.0f;
	voice.envelope = glirps::EnvelopeSpec{.attack_seconds = 0.002f,
		.decay_seconds = 0.006f,
		.sustain_level = 1.0f,
		.release_seconds = 0.006f};
	glirps::test_support::setVoices(program_storage, {voice});

	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	ASSERT_TRUE(synth.start(program, 0).has_value());

	std::vector<float> output(48000);

	// ACT
	synth.render(output);

	// ASSERT: the output reflects the program's base pitch, not just any
	// non-silence. The base pitch C-3 is 130.81 Hz, so a one-second render
	// crosses zero about 262 times.
	std::uint32_t zero_crossings = 0;
	for (std::size_t i = 1; i < output.size(); ++i)
	{
		if ((output.at(i - 1) < 0.0f) != (output.at(i) < 0.0f))
			++zero_crossings;
	}
	EXPECT_NEAR(static_cast<float>(zero_crossings), 2.0f * 130.81f, 40.0f);
}

// `applyBlock` with a block targeting a released handle changes nothing and
// does not touch the slot's new occupant.
TEST(SynthTests, ApplyBlock_ReleasedHandle_IsIgnored)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	glirps::SoundProgram& program = program_storage.program;
	glirps::test_support::resizeVoices(program_storage, 1);
	program_storage.voice_storage.at(0).waveform = glirps::Waveform::triangle;

	auto first = synth.start(program, 0);
	ASSERT_TRUE(first.has_value());
	synth.release(*first);

	std::vector<float> acknowledge_release(glirps::sub_block_frame_count, 0.0f);
	synth.render(acknowledge_release);
	auto second = synth.start(program, 0);
	ASSERT_TRUE(second.has_value());

	// Build a block targeting the released (stale) handle with a different
	// frequency than the base pitch would produce.
	glirps::ParameterBlock stale_block;
	stale_block.target = *first;
	stale_block.voice_count = 1;
	stale_block.voices.at(0).waveform = glirps::Waveform::triangle;
	stale_block.voices.at(0).frequency_hz = 20000.0f;
	stale_block.voices.at(0).gate_on = true;

	// ACT
	synth.applyBlock(stale_block);

	std::vector<float> output(48000);
	synth.render(output);

	// ASSERT: the new occupant renders its own base pitch, so the stale block
	// was ignored. A C-3 tone (130.81 Hz) crosses zero about 262 times in one
	// second; a 20 kHz tone would cross about 40000 times.
	std::uint32_t zero_crossings = 0;
	for (std::size_t i = 1; i < output.size(); ++i)
	{
		if ((output.at(i - 1) < 0.0f) != (output.at(i) < 0.0f))
			++zero_crossings;
	}
	EXPECT_LT(zero_crossings, 1000u);
}

// `configure` with a different rate while an instance is playing reports that
// instance complete and leaves the pool at full capacity.
TEST(SynthTests, Configure_WithLiveInstances_DiscardsAndReportsComplete)
{
	// ARRANGE
	glirps::impl::Synth synth(2, 8);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	glirps::SoundProgram& program = program_storage.program;
	glirps::test_support::resizeVoices(program_storage, 1);

	auto first = synth.start(program, 0);
	auto second = synth.start(program, 0);
	ASSERT_TRUE(first.has_value());
	ASSERT_TRUE(second.has_value());

	// ACT: reconfigure at a different rate.
	synth.configure(24000);

	// ASSERT
	EXPECT_TRUE(synth.isComplete(*first));
	EXPECT_TRUE(synth.isComplete(*second));
	// Pool at full capacity: two fresh starts succeed.
	auto third = synth.start(program, 0);
	auto fourth = synth.start(program, 0);
	EXPECT_TRUE(third.has_value());
	EXPECT_TRUE(fourth.has_value());
}

// A rate-and-program quantity converts at `start`: an envelope duration in
// seconds measures a different sample count at each configured rate while the
// duration in seconds is unchanged.
TEST(SynthTests, Start_EnvelopeSeconds_ConvertAtConfiguredRate)
{
	// ARRANGE
	// A pulse voice with a long, linear attack.
	glirps::test_support::TestProgramStorage program_storage;
	glirps::SoundProgram& program = program_storage.program;
	program.base_pitch = glirps::Pitch{.semitone = 36, .cents = 0};
	glirps::VoiceProgram voice;
	voice.waveform = glirps::Waveform::pulse;
	voice.pulse_width = 0.5f;
	voice.gain = 1.0f;
	voice.envelope = glirps::EnvelopeSpec{.attack_seconds = 0.1f,
		.decay_seconds = 0.5f,
		.sustain_level = 1.0f,
		.release_seconds = 0.5f};
	glirps::test_support::setVoices(program_storage, {voice});

	constexpr float lower_level = 0.25f;
	constexpr float upper_level = 0.75f;

	// The attack is linear over `attack_seconds`, so the samples between the
	// two level crossings measure `(upper - lower) * attack_seconds * rate`.
	const auto measure_rise = [&](std::uint32_t rate) -> float
	{
		glirps::impl::Synth synth(1, 4);
		synth.configure(rate);
		auto handle = synth.start(program, 0);
		EXPECT_TRUE(handle.has_value());
		if (!handle)
			return 0.0f;

		const std::uint32_t frame_count = rate * 2;
		std::vector<float> output(frame_count);
		synth.render(output);

		const float peak = peakOf(output);
		const float low_threshold = lower_level * peak;
		const float high_threshold = upper_level * peak;
		std::uint32_t first_low = 0;
		std::uint32_t first_high = 0;
		for (std::uint32_t i = 0; i < frame_count; ++i)
		{
			if (first_low == 0 && std::fabs(output.at(i)) >= low_threshold)
				first_low = i;
			if (std::fabs(output.at(i)) >= high_threshold)
			{
				first_high = i;
				break;
			}
		}
		return static_cast<float>(first_high) - static_cast<float>(first_low);
	};

	// ACT
	const float rise_48k = measure_rise(48000);
	const float rise_24k = measure_rise(24000);

	// ASSERT
	// The rise in samples doubles when the rate halves, so the duration in
	// seconds is unchanged. The attack is 0.1 s; a full attack spans
	// 0.1 * rate samples and the two crossings span half of it.
	const float expected_48k = 0.1f * 48000.0f * (upper_level - lower_level);
	const float expected_24k = 0.1f * 24000.0f * (upper_level - lower_level);
	EXPECT_NEAR(rise_48k, expected_48k, expected_48k * 0.2f);
	EXPECT_NEAR(rise_24k, expected_24k, expected_24k * 0.2f);
	// The sample count scales with the rate while the duration is unchanged.
	EXPECT_NEAR(rise_48k / rise_24k, 2.0f, 0.2f);
}

namespace
{
	// Counts rising zero crossings and converts to an approximate frequency,
	// used to observe a rendered voice's pitch without an internal accessor.
	float estimateFrequencyHz(const std::vector<float>& samples, std::uint32_t sample_rate_hz)
	{
		std::uint32_t crossings = 0;
		for (std::size_t i = 1; i < samples.size(); ++i)
		{
			if (samples.at(i - 1) <= 0.0f && samples.at(i) > 0.0f)
				++crossings;
		}
		return static_cast<float>(crossings) * static_cast<float>(sample_rate_hz) / static_cast<float>(samples.size());
	}

	// A one-voice program with an instantly-attacking, fully-sustained
	// envelope and unity gains, isolating the field under test from the
	// envelope shape.
	glirps::SoundProgram sustainedTriangleProgram(glirps::test_support::TestProgramStorage& storage)
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

	// Frames spanned by one base tick at the test sample rate.
	constexpr std::uint32_t frames_per_base_tick_synth = test_sample_rate_hz_synth / glirps::control_grid_base_rate_hz;
}

// `Synth::start`'s gain multiplier scales the instance's authored chip gain
// on its first block, clamped to the gain range, so a trigger carrying a
// `gain_multiplier` is quieter without a second authored sound.
TEST(SynthTests, Start_GainMultiplier_ScalesFirstBlockOutputGain)
{
	// ARRANGE
	glirps::impl::Synth synth(2, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.output_gain = 1.0f;
	std::array<glirps::ParameterBlock, 1> blocks;

	// ACT: start the same program once at half gain and once at unity gain.
	auto scaled = synth.start(program,
		0,
		0,
		glirps::ControlParams{.flags = glirps::ControlFlag::gain_multiplier, .gain_multiplier = 0.5f});
	ASSERT_TRUE(scaled.has_value());
	std::ignore = synth.evaluateTick(*scaled, blocks);
	const float scaled_gain = blocks.at(0).output_gain;

	synth.release(*scaled);
	std::vector<float> acknowledge_release(glirps::sub_block_frame_count, 0.0f);
	synth.render(acknowledge_release);
	auto unity = synth.start(program, 0);
	ASSERT_TRUE(unity.has_value());
	std::ignore = synth.evaluateTick(*unity, blocks);
	const float unity_gain = blocks.at(0).output_gain;

	// ASSERT: half gain reads as half of unity within the 4-bit gain step.
	EXPECT_NEAR(scaled_gain, unity_gain * 0.5f, glirps::gain_authored_ceiling / 16.0f);
	EXPECT_LT(scaled_gain, unity_gain);
	EXPECT_LE(unity_gain, glirps::gain_authored_ceiling);
}

// `getBlocksPerBaseTick` returns the effective tick multiplier and
// `evaluateTick` writes exactly that many blocks, one per evenly spaced
// evaluation within the base tick.
TEST(SynthTests, GetBlocksPerBaseTick_ReturnsMultiplier_EvaluateTickWritesThatMany)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.grit.tick_multiplier = 4;
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	// ACT
	const std::uint32_t blocks_per_tick = synth.getBlocksPerBaseTick(*handle);
	std::vector<glirps::ParameterBlock> blocks(blocks_per_tick);
	const std::uint32_t written = synth.evaluateTick(*handle, blocks);

	// ASSERT
	EXPECT_EQ(blocks_per_tick, 4u);
	EXPECT_EQ(written, 4u);
}

// At 48 kHz with a tick multiplier of 4, one base tick is 960 frames (15
// sub-blocks) split across 4 effective ticks of 240 frames each, which is
// not a whole number of sub-blocks: successive boundaries are 0, 3, 7, 11,
// 15.
TEST(SynthTests, EvaluateTick_SubBlockSpansRequested_MatchGridPlacement)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.grit.tick_multiplier = 4;
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	// ACT
	const std::uint32_t blocks_per_tick = synth.getBlocksPerBaseTick(*handle);
	std::vector<glirps::ParameterBlock> blocks(blocks_per_tick);
	std::vector<glirps::TickSubBlockSpan> spans(blocks_per_tick);
	const std::uint32_t written = synth.evaluateTick(*handle, blocks, spans);

	// ASSERT
	ASSERT_EQ(written, 4u);
	const std::array<std::uint64_t, 5> expected_boundaries{0, 3, 7, 11, 15};
	for (std::uint32_t t = 0; t < written; ++t)
	{
		EXPECT_EQ(spans.at(t).start, expected_boundaries.at(t));
		EXPECT_EQ(spans.at(t).next_start, expected_boundaries.at(t + 1));
	}
}

// A `sub_block_spans` shorter than the number of blocks written only receives
// entries for as many blocks as it holds; every block past that point is
// still evaluated normally rather than the per-tick loop stopping early, per
// the doc comment's "first `written` entries" contract on `evaluateTick`.
TEST(SynthTests, EvaluateTick_SubBlockSpansShorterThanBlocks_LaterBlocksStillWritten)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.grit.tick_multiplier = 4;
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());
	const std::uint32_t blocks_per_tick = synth.getBlocksPerBaseTick(*handle);
	ASSERT_EQ(blocks_per_tick, 4u);
	std::vector<glirps::ParameterBlock> blocks(blocks_per_tick);
	std::vector<glirps::TickSubBlockSpan> spans(2);

	// ACT
	const std::uint32_t written = synth.evaluateTick(*handle, blocks, spans);

	// ASSERT
	// All 4 blocks are written despite only 2 spans being available.
	ASSERT_EQ(written, 4u);
	const std::array<std::uint64_t, 3> expected_boundaries{0, 3, 7};
	for (std::uint32_t t = 0; t < spans.size(); ++t)
	{
		EXPECT_EQ(spans.at(t).start, expected_boundaries.at(t));
		EXPECT_EQ(spans.at(t).next_start, expected_boundaries.at(t + 1));
	}
	// Blocks 2 and 3, past the spans' capacity, are still fully evaluated
	// rather than left at their default-constructed (gated off) state.
	EXPECT_TRUE(blocks.at(2).voices.at(0).gate_on);
	EXPECT_TRUE(blocks.at(3).voices.at(0).gate_on);
	EXPECT_GT(blocks.at(3).voices.at(0).frequency_hz, 0.0f);
}

// A table of four one-tick values looping to its third value yields values
// 0, 1, 2, 3, 2, 3 over six ticks.
TEST(SynthTests, EvaluateTick_TableWithLoopPoint_YieldsExpectedValueSequence)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program_storage.voice_storage.at(0).tables.has_arpeggio = true;
	program_storage.voice_storage.at(0).tables.arpeggio
		= glirps::test_support::makeIntTable(program_storage, {0, 1, 2, 3}, 2);
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	const std::array<std::int32_t, 6> expected_offsets{0, 1, 2, 3, 2, 3};

	// ACT & ASSERT
	for (const std::int32_t expected_offset : expected_offsets)
	{
		std::array<glirps::ParameterBlock, 1> blocks;
		ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
		const float expected_frequency = glirps::frequencyHertz(36 + expected_offset, 0.0f);
		EXPECT_NEAR(blocks.at(0).voices.at(0).frequency_hz, expected_frequency, 0.01f);
	}
}

// An arpeggio table and a gain table of different lengths and loop points
// each move on by one value per tick on their own table time, so neither wraps
// at the other's length: each table is evaluated independently.
TEST(SynthTests, EvaluateTick_TablesOfDifferentLengths_AdvanceIndependently)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program_storage.voice_storage.at(0).tables.has_arpeggio = true;
	program_storage.voice_storage.at(0).tables.arpeggio
		= glirps::test_support::makeIntTable(program_storage, {0, 4, 7}, 0);
	program_storage.voice_storage.at(0).tables.has_gain = true;
	program_storage.voice_storage.at(0).tables.gain
		= glirps::test_support::makeFloatTable(program_storage, {1.0f, 0.8f, 0.6f, 0.4f, 0.2f}, 4);
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	const std::array<std::int32_t, 6> expected_offsets{0, 4, 7, 0, 4, 7};
	const std::array<float, 6> expected_gains{1.0f, 0.8f, 0.6f, 0.4f, 0.2f, 0.2f};

	// ACT & ASSERT
	for (std::size_t i = 0; i < expected_offsets.size(); ++i)
	{
		std::array<glirps::ParameterBlock, 1> blocks;
		ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
		const float expected_frequency = glirps::frequencyHertz(36 + expected_offsets.at(i), 0.0f);
		EXPECT_NEAR(blocks.at(0).voices.at(0).frequency_hz, expected_frequency, 0.01f);
		EXPECT_NEAR(blocks.at(0).voices.at(0).gain, expected_gains.at(i), 0.0001f);
	}
}

// An arpeggio table of 0, 4, 7 over a fixed base pitch yields the three
// expected frequencies within 0.1 percent.
TEST(SynthTests, EvaluateTick_ArpeggioTable_YieldsExpectedFrequencies)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program_storage.voice_storage.at(0).tables.has_arpeggio = true;
	program_storage.voice_storage.at(0).tables.arpeggio
		= glirps::test_support::makeIntTable(program_storage, {0, 4, 7}, 0);
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	// ACT & ASSERT
	for (const std::int32_t offset : {0, 4, 7})
	{
		std::array<glirps::ParameterBlock, 1> blocks;
		ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
		const float expected_frequency = glirps::frequencyHertz(36 + offset, 0.0f);
		EXPECT_NEAR(blocks.at(0).voices.at(0).frequency_hz, expected_frequency, expected_frequency * 0.001f);
	}
}

// A `pitch_slide` table accumulates across ticks and a `pulse_width_delta`
// table accumulates and clamps at 0 and 1.
TEST(SynthTests, EvaluateTick_PitchSlideAndPulseWidthDelta_AccumulateAndClamp)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program_storage.voice_storage.at(0).pulse_width = 0.5f;
	program_storage.voice_storage.at(0).tables.has_pitch_slide = true;
	program_storage.voice_storage.at(0).tables.pitch_slide
		= glirps::test_support::makeFloatTable(program_storage, {-10.0f, -10.0f}, 1);
	program_storage.voice_storage.at(0).tables.has_pulse_width_delta = true;
	program_storage.voice_storage.at(0).tables.pulse_width_delta
		= glirps::test_support::makeFloatTable(program_storage, {0.6f, 0.6f}, 1);
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	// ACT
	std::array<glirps::ParameterBlock, 1> tick1;
	ASSERT_EQ(synth.evaluateTick(*handle, tick1), 1u);
	std::array<glirps::ParameterBlock, 1> tick2;
	ASSERT_EQ(synth.evaluateTick(*handle, tick2), 1u);

	// ASSERT
	const float expected_tick1 = glirps::frequencyHertz(36, -10.0f);
	const float expected_tick2 = glirps::frequencyHertz(36, -20.0f);
	EXPECT_NEAR(tick1.at(0).voices.at(0).frequency_hz, expected_tick1, 0.01f);
	EXPECT_NEAR(tick2.at(0).voices.at(0).frequency_hz, expected_tick2, 0.01f);
	// 0.5 + 0.6 clamps to 1.0 on the first tick and stays there.
	EXPECT_FLOAT_EQ(tick1.at(0).voices.at(0).pulse_width, 1.0f);
	EXPECT_FLOAT_EQ(tick2.at(0).voices.at(0).pulse_width, 1.0f);
}

// `Synth::updateControlParams` shifts a sustained instance's pitch by a
// flagged per-tick cents offset and overrides its filter cutoff when the
// override is flagged, rather than through time-based tables.
TEST(SynthTests, UpdateControlParams_ShiftsPitchAndOverridesCutoff)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.filter.cutoff_hz = 800.0f;
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());
	std::array<glirps::ParameterBlock, 1> blocks;

	// Baseline: authored pitch and cutoff.
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
	const float baseline_frequency = glirps::frequencyHertz(36, 0.0f);
	EXPECT_NEAR(blocks.at(0).voices.at(0).frequency_hz, baseline_frequency, 0.01f);
	EXPECT_FLOAT_EQ(blocks.at(0).cutoff_hz, 800.0f);

	// ACT: a flagged +1200c pitch shift and a flagged 2400 Hz cutoff override.
	synth.updateControlParams(*handle,
		glirps::ControlParams{
			.flags = glirps::ControlFlag::pitch_cents | glirps::ControlFlag::cutoff_hz,
			.pitch_cents = 1200.0f,
			.cutoff_hz = 2400.0f,
		});
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);

	// ASSERT
	const float shifted_frequency = glirps::frequencyHertz(36, 1200.0f);
	EXPECT_NEAR(blocks.at(0).voices.at(0).frequency_hz, shifted_frequency, 0.01f);
	EXPECT_FLOAT_EQ(blocks.at(0).cutoff_hz, 2400.0f);

	// An explicit flagged zero pitch offset restores the authored pitch, but
	// the cutoff override is unflagged this call and so stays as it was:
	// `reset_flags` is the explicit clear operation of `updateControlParams` and
	// it is not set here.
	synth.updateControlParams(*handle,
		glirps::ControlParams{.flags = glirps::ControlFlag::pitch_cents, .pitch_cents = 0.0f});
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
	EXPECT_NEAR(blocks.at(0).voices.at(0).frequency_hz, baseline_frequency, 0.01f);
	EXPECT_FLOAT_EQ(blocks.at(0).cutoff_hz, 2400.0f);
}

// The flagged gain multiplier scales the instance's chip output gain every
// tick. A multiplier of 1 leaves the authored level alone and 0
// silences the instance without releasing its gate.
TEST(SynthTests, UpdateControlParams_ScalesChipOutputGainPerTick)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.output_gain = 0.8f;
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());
	std::array<glirps::ParameterBlock, 1> blocks;

	// Baseline: the authored chip gain.
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
	EXPECT_FLOAT_EQ(blocks.at(0).output_gain, 0.8f);

	// ACT
	synth.updateControlParams(*handle,
		glirps::ControlParams{.flags = glirps::ControlFlag::gain_multiplier, .gain_multiplier = 0.25f});
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);

	// ASSERT
	EXPECT_FLOAT_EQ(blocks.at(0).output_gain, 0.2f);

	// A zero multiplier silences the instance while its voices stay gated.
	synth.updateControlParams(*handle,
		glirps::ControlParams{.flags = glirps::ControlFlag::gain_multiplier, .gain_multiplier = 0.0f});
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
	EXPECT_FLOAT_EQ(blocks.at(0).output_gain, 0.0f);
	EXPECT_TRUE(blocks.at(0).voices.at(0).gate_on);

	// Back to 1, the authored level returns.
	synth.updateControlParams(*handle,
		glirps::ControlParams{.flags = glirps::ControlFlag::gain_multiplier, .gain_multiplier = 1.0f});
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
	EXPECT_FLOAT_EQ(blocks.at(0).output_gain, 0.8f);
}

// A gain-only update leaves a previously set pitch offset and cutoff
// override in place: an unflagged field keeps the instance's current value
// rather than reverting to the authored one.
TEST(SynthTests, UpdateControlParams_LeavesUnflaggedFieldsUnchanged)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.filter.cutoff_hz = 800.0f;
	program.output_gain = 0.8f;
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());
	std::array<glirps::ParameterBlock, 1> blocks;

	// Baseline: a flagged pitch shift and cutoff override.
	synth.updateControlParams(*handle,
		glirps::ControlParams{
			.flags = glirps::ControlFlag::pitch_cents | glirps::ControlFlag::cutoff_hz,
			.pitch_cents = 1200.0f,
			.cutoff_hz = 2400.0f,
		});
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
	const float shifted_frequency = glirps::frequencyHertz(36, 1200.0f);
	ASSERT_NEAR(blocks.at(0).voices.at(0).frequency_hz, shifted_frequency, 0.01f);
	ASSERT_FLOAT_EQ(blocks.at(0).cutoff_hz, 2400.0f);

	// ACT: a gain-only update.
	synth.updateControlParams(*handle,
		glirps::ControlParams{.flags = glirps::ControlFlag::gain_multiplier, .gain_multiplier = 0.5f});
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);

	// ASSERT: the pitch offset and cutoff override are unchanged, and gain
	// is scaled.
	EXPECT_NEAR(blocks.at(0).voices.at(0).frequency_hz, shifted_frequency, 0.01f);
	EXPECT_FLOAT_EQ(blocks.at(0).cutoff_hz, 2400.0f);
	EXPECT_FLOAT_EQ(blocks.at(0).output_gain, 0.4f);
}

// `start`'s `params` establishes the instance's initial control state: the
// first evaluated tick already carries the flagged pitch, cutoff, and gain,
// with no `updateControlParams` call.
TEST(SynthTests, Start_AppliesInitialControlParams)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.filter.cutoff_hz = 800.0f;
	program.output_gain = 0.8f;
	std::array<glirps::ParameterBlock, 1> blocks;

	// ACT
	auto handle = synth.start(program,
		0,
		0,
		glirps::ControlParams{
			.flags
			= glirps::ControlFlag::pitch_cents | glirps::ControlFlag::cutoff_hz | glirps::ControlFlag::gain_multiplier,
			.pitch_cents = 1200.0f,
			.cutoff_hz = 2400.0f,
			.gain_multiplier = 0.5f,
		});
	ASSERT_TRUE(handle.has_value());
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);

	// ASSERT
	const float shifted_frequency = glirps::frequencyHertz(36, 1200.0f);
	EXPECT_NEAR(blocks.at(0).voices.at(0).frequency_hz, shifted_frequency, 0.01f);
	EXPECT_FLOAT_EQ(blocks.at(0).cutoff_hz, 2400.0f);
	EXPECT_FLOAT_EQ(blocks.at(0).output_gain, 0.4f);
}

// A non-finite flagged field in `start`'s `params` is ignored rather than
// stored, so the first evaluated tick carries the authored pitch, cutoff, and
// gain.
TEST(SynthTests, Start_IgnoresNonFiniteControlParams)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.filter.cutoff_hz = 800.0f;
	program.output_gain = 0.8f;
	std::array<glirps::ParameterBlock, 1> blocks;
	const float nan = std::numeric_limits<float>::quiet_NaN();

	// ACT
	auto handle = synth.start(program,
		0,
		0,
		glirps::ControlParams{
			.flags
			= glirps::ControlFlag::pitch_cents | glirps::ControlFlag::cutoff_hz | glirps::ControlFlag::gain_multiplier,
			.pitch_cents = nan,
			.cutoff_hz = nan,
			.gain_multiplier = nan,
		});
	ASSERT_TRUE(handle.has_value());
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);

	// ASSERT
	const float baseline_frequency = glirps::frequencyHertz(36, 0.0f);
	EXPECT_NEAR(blocks.at(0).voices.at(0).frequency_hz, baseline_frequency, 0.01f);
	EXPECT_FLOAT_EQ(blocks.at(0).cutoff_hz, 800.0f);
	EXPECT_FLOAT_EQ(blocks.at(0).output_gain, 0.8f);
}

// `updateControlParams`'s `reset_flags` restores pitch, cutoff, and gain to
// their authored state after a prior flagged override.
TEST(SynthTests, UpdateControlParams_ResetFlagsRestoreAuthoredValues)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.filter.cutoff_hz = 800.0f;
	program.output_gain = 0.8f;
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());
	std::array<glirps::ParameterBlock, 1> blocks;

	// Baseline: a flagged pitch shift, cutoff override, and gain scale.
	synth.updateControlParams(*handle,
		glirps::ControlParams{
			.flags
			= glirps::ControlFlag::pitch_cents | glirps::ControlFlag::cutoff_hz | glirps::ControlFlag::gain_multiplier,
			.pitch_cents = 1200.0f,
			.cutoff_hz = 2400.0f,
			.gain_multiplier = 0.5f,
		});
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
	ASSERT_FLOAT_EQ(blocks.at(0).cutoff_hz, 2400.0f);
	ASSERT_FLOAT_EQ(blocks.at(0).output_gain, 0.4f);

	// ACT: reset all three fields, carrying no flagged values.
	synth.updateControlParams(*handle,
		glirps::ControlParams{.reset_flags = glirps::ControlFlag::pitch_cents | glirps::ControlFlag::cutoff_hz
				| glirps::ControlFlag::gain_multiplier});
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);

	// ASSERT: every field is back to its authored value.
	const float authored_frequency = glirps::frequencyHertz(36, 0.0f);
	EXPECT_NEAR(blocks.at(0).voices.at(0).frequency_hz, authored_frequency, 0.01f);
	EXPECT_FLOAT_EQ(blocks.at(0).cutoff_hz, 800.0f);
	EXPECT_FLOAT_EQ(blocks.at(0).output_gain, 0.8f);
}

// A field named in both `flags` and `reset_flags` in the same call resets
// rather than applying the flagged value.
TEST(SynthTests, UpdateControlParams_ResetFlagsTakePrecedenceOverFlags)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.filter.cutoff_hz = 800.0f;
	program.output_gain = 0.8f;
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());
	std::array<glirps::ParameterBlock, 1> blocks;

	// ACT: every field is both flagged with a value and named in reset_flags.
	const std::uint32_t all_fields
		= glirps::ControlFlag::pitch_cents | glirps::ControlFlag::cutoff_hz | glirps::ControlFlag::gain_multiplier;
	synth.updateControlParams(*handle,
		glirps::ControlParams{
			.flags = all_fields,
			.reset_flags = all_fields,
			.pitch_cents = 1200.0f,
			.cutoff_hz = 2400.0f,
			.gain_multiplier = 0.5f,
		});
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);

	// ASSERT: the reset wins for every field; the flagged values are ignored.
	const float authored_frequency = glirps::frequencyHertz(36, 0.0f);
	EXPECT_NEAR(blocks.at(0).voices.at(0).frequency_hz, authored_frequency, 0.01f);
	EXPECT_FLOAT_EQ(blocks.at(0).cutoff_hz, 800.0f);
	EXPECT_FLOAT_EQ(blocks.at(0).output_gain, 0.8f);
}

// `start` does not read `reset_flags`: a freshly claimed slot already begins
// at the authored state reset_flags would restore, so a start-time call
// carrying both flagged values and reset_flags applies the flagged values
// unaffected.
TEST(SynthTests, Start_IgnoresResetFlags)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.filter.cutoff_hz = 800.0f;
	program.output_gain = 0.8f;
	std::array<glirps::ParameterBlock, 1> blocks;
	const std::uint32_t all_fields
		= glirps::ControlFlag::pitch_cents | glirps::ControlFlag::cutoff_hz | glirps::ControlFlag::gain_multiplier;

	// ACT
	auto handle = synth.start(program,
		0,
		0,
		glirps::ControlParams{
			.flags = all_fields,
			.reset_flags = all_fields,
			.pitch_cents = 1200.0f,
			.cutoff_hz = 2400.0f,
			.gain_multiplier = 0.5f,
		});
	ASSERT_TRUE(handle.has_value());
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);

	// ASSERT: the flagged values apply exactly as they would without
	// reset_flags set.
	const float shifted_frequency = glirps::frequencyHertz(36, 1200.0f);
	EXPECT_NEAR(blocks.at(0).voices.at(0).frequency_hz, shifted_frequency, 0.01f);
	EXPECT_FLOAT_EQ(blocks.at(0).cutoff_hz, 2400.0f);
	EXPECT_FLOAT_EQ(blocks.at(0).output_gain, 0.4f);
}

// A long descending `pitch_slide` drives `frequency_hz` to exactly 0 and
// never below, a long ascending one stops at half the configured sample
// rate, and a voice at 0 Hz keeps advancing its envelope rather than falling
// silent: a long slide reaching the end of the range is authored content
// behaving as written, not an error. A voice at 0 Hz holds its phase but
// its envelope runs on, so it decays rather than stopping abruptly.
TEST(SynthTests, EvaluateTick_LongSlide_ClampsFrequencyAndKeepsEnvelopeRunning)
{
	// ARRANGE
	glirps::impl::Synth descending_synth(1, 4);
	descending_synth.configure(test_sample_rate_hz_synth);
	descending_synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage descending_program_storage;
	auto descending_program = sustainedTriangleProgram(descending_program_storage);
	descending_program_storage.voice_storage.at(0).tables.has_pitch_slide = true;
	descending_program_storage.voice_storage.at(0).tables.pitch_slide
		= glirps::test_support::makeFloatTable(descending_program_storage, {-2000.0f}, 0);
	auto descending_handle = descending_synth.start(descending_program, 0);
	ASSERT_TRUE(descending_handle.has_value());

	glirps::impl::Synth ascending_synth(1, 4);
	ascending_synth.configure(test_sample_rate_hz_synth);
	ascending_synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage ascending_program_storage;
	auto ascending_program = sustainedTriangleProgram(ascending_program_storage);
	ascending_program_storage.voice_storage.at(0).tables.has_pitch_slide = true;
	ascending_program_storage.voice_storage.at(0).tables.pitch_slide
		= glirps::test_support::makeFloatTable(ascending_program_storage, {20000.0f}, 0);
	auto ascending_handle = ascending_synth.start(ascending_program, 0);
	ASSERT_TRUE(ascending_handle.has_value());

	// ACT: enough ticks that the slide has long since walked past either
	// bound.
	constexpr std::uint32_t tick_count = 200;
	float last_descending_frequency = -1.0f;
	float last_ascending_frequency = -1.0f;
	for (std::uint32_t t = 0; t < tick_count; ++t)
	{
		std::array<glirps::ParameterBlock, 1> descending_blocks;
		ASSERT_EQ(descending_synth.evaluateTick(*descending_handle, descending_blocks), 1u);
		last_descending_frequency = descending_blocks.at(0).voices.at(0).frequency_hz;
		descending_synth.applyBlock(descending_blocks.at(0));

		std::array<glirps::ParameterBlock, 1> ascending_blocks;
		ASSERT_EQ(ascending_synth.evaluateTick(*ascending_handle, ascending_blocks), 1u);
		last_ascending_frequency = ascending_blocks.at(0).voices.at(0).frequency_hz;
	}

	// ASSERT
	EXPECT_FLOAT_EQ(last_descending_frequency, 0.0f);
	EXPECT_FLOAT_EQ(last_ascending_frequency, static_cast<float>(test_sample_rate_hz_synth) / 2.0f);

	// ACT: the clamped-to-zero voice keeps rendering under its envelope
	// rather than going silent because its oscillator held.
	std::vector<float> output(48000);
	descending_synth.render(output);

	// ASSERT
	EXPECT_GT(peakOf(output), 0.0f);
}

// Every field of an emitted parameter block lies inside its `ParameterBlock`
// field range, driven by a sound whose tables push each bounded field past
// both of its ends. Every field is clamped after the grit stage.
TEST(SynthTests, EvaluateTick_TablesPushBoundedFieldsPastBothEnds_StaysInRange)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program_storage.voice_storage.at(0).tables.has_gain = true;
	program_storage.voice_storage.at(0).tables.gain
		= glirps::test_support::makeFloatTable(program_storage, {-5.0f, 5.0f}, 1);
	program_storage.voice_storage.at(0).tables.has_pulse_width_delta = true;
	program_storage.voice_storage.at(0).tables.pulse_width_delta
		= glirps::test_support::makeFloatTable(program_storage, {-5.0f, 10.0f}, 1);
	program.filter.tables.has_cutoff = true;
	program.filter.tables.cutoff = glirps::test_support::makeFloatTable(program_storage, {-5.0f, 999999.0f}, 1);
	program.filter.tables.has_resonance = true;
	program.filter.tables.resonance = glirps::test_support::makeFloatTable(program_storage, {-5.0f, 999.0f}, 1);
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	const float cutoff_ceiling = static_cast<float>(test_sample_rate_hz_synth) * 0.49f;
	const float nyquist = static_cast<float>(test_sample_rate_hz_synth) / 2.0f;

	// ACT & ASSERT
	for (int t = 0; t < 4; ++t)
	{
		std::array<glirps::ParameterBlock, 1> blocks;
		ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
		const glirps::ParameterBlock& block = blocks.at(0);
		EXPECT_GE(block.cutoff_hz, 1.0f);
		EXPECT_LE(block.cutoff_hz, cutoff_ceiling);
		EXPECT_GE(block.resonance_q, 0.5f);
		EXPECT_LE(block.resonance_q, 20.0f);
		EXPECT_GE(block.voices.at(0).gain, 0.0f);
		EXPECT_LE(block.voices.at(0).gain, glirps::gain_ceiling);
		EXPECT_GE(block.voices.at(0).pulse_width, 0.0f);
		EXPECT_LE(block.voices.at(0).pulse_width, 1.0f);
		EXPECT_GE(block.voices.at(0).frequency_hz, 0.0f);
		EXPECT_LE(block.voices.at(0).frequency_hz, nyquist);
	}
}

// A sound authoring no tables holds every parameter at its base value across
// many ticks, and its output is not silence: an absent table is not a table
// of zeros.
TEST(SynthTests, EvaluateTick_NoTables_HoldsBaseValues)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program_storage.voice_storage.at(0).pulse_width = 0.5f;
	program_storage.voice_storage.at(0).gain = 0.7f;
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());
	const float base_frequency = glirps::frequencyHertz(program.base_pitch);

	// ACT & ASSERT
	for (int t = 0; t < 20; ++t)
	{
		std::array<glirps::ParameterBlock, 1> blocks;
		ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
		const glirps::ParameterBlock& block = blocks.at(0);
		EXPECT_NEAR(block.voices.at(0).frequency_hz, base_frequency, 0.01f);
		EXPECT_FLOAT_EQ(block.voices.at(0).pulse_width, 0.5f);
		EXPECT_FLOAT_EQ(block.voices.at(0).gain, 0.7f);
		synth.applyBlock(block);
	}

	std::vector<float> output(48000);
	synth.render(output);
	EXPECT_GT(peakOf(output), 0.0f);
}

// A frequency change carried by a parameter block takes effect starting at
// the exact sample the block was applied for, rather than gradually: pitch
// is not ramped. Rendering one base
// tick's worth of samples per `evaluateTick`/`applyBlock` pair, an arpeggio
// jump of one octave doubles the measured frequency starting in the segment
// it was applied to.
TEST(SynthTests, EvaluateTick_FrequencyChange_TakesEffectAtBoundary)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.base_pitch.semitone = 48;  // C-4.
	// Bypass the filter: it is irrelevant to what this test measures, and its
	// default lowpass otherwise rings enough near a waveform's zero crossing
	// to add spurious crossings to the frequency estimate.
	program.filter.modes
		= glirps::FilterModeSet{.lowpass = false, .bandpass = false, .highpass = false, .notch = false};
	program_storage.voice_storage.at(0).tables.has_arpeggio = true;
	program_storage.voice_storage.at(0).tables.arpeggio
		= glirps::test_support::makeIntTable(program_storage, {0, 12}, 1);
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	// ACT: evaluate and apply the first (unchanged) tick, then render and
	// discard one warm-up tick, so the envelope's attack and the
	// decimator's settling transient (both expected startup behavior,
	// neither part of what this test measures) land outside the segments
	// measured below. Each measured segment is many multiples of the base
	// tick's length, both to give the zero-crossing estimate enough cycles
	// for a stable reading, and to prove the new pitch holds rather than
	// reverting once the tick that carried it ends.
	constexpr std::uint32_t measurement_frames = frames_per_base_tick_synth * 8;
	std::array<glirps::ParameterBlock, 1> block0;
	ASSERT_EQ(synth.evaluateTick(*handle, block0), 1u);
	synth.applyBlock(block0.at(0));
	std::vector<float> warm_up(frames_per_base_tick_synth);
	synth.render(warm_up);

	std::vector<float> segment0(measurement_frames);
	synth.render(segment0);

	std::array<glirps::ParameterBlock, 1> block1;
	ASSERT_EQ(synth.evaluateTick(*handle, block1), 1u);
	synth.applyBlock(block1.at(0));
	std::vector<float> segment1(measurement_frames);
	synth.render(segment1);

	// ASSERT
	const float frequency0 = estimateFrequencyHz(segment0, test_sample_rate_hz_synth);
	const float frequency1 = estimateFrequencyHz(segment1, test_sample_rate_hz_synth);
	EXPECT_NEAR(frequency1 / frequency0, 2.0f, 0.1f);
}

// A table of several hundred values loads and renders without a length error:
// `glirps` imposes no compile-time bound on table length.
TEST(SynthTests, EvaluateTick_TableOfSeveralHundredValues_LoadsAndRendersWithoutError)
{
	// ARRANGE
	constexpr std::size_t long_table_row_count = 500;
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	std::vector<float> gain_rows(long_table_row_count);
	for (std::size_t i = 0; i < long_table_row_count; ++i)
		gain_rows.at(i) = 1.0f - static_cast<float>(i % 2) * 0.5f;
	program_storage.voice_storage.at(0).tables.has_gain = true;
	program_storage.voice_storage.at(0).tables.gain
		= glirps::test_support::makeFloatTable(program_storage, std::move(gain_rows), 0);
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	// ACT & ASSERT
	for (std::size_t t = 0; t < long_table_row_count; ++t)
	{
		std::array<glirps::ParameterBlock, 1> blocks;
		ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
		synth.applyBlock(blocks.at(0));
	}
	std::vector<float> output(48000);
	synth.render(output);
	EXPECT_GT(peakOf(output), 0.0f);
}

// One `SoundProgram` rendered at grit scale 0 and at grit scale 1 produces
// different sample buffers through the same code path: the grit scale is a
// property of the `Synth`, not of the program.
TEST(SynthTests, EvaluateTick_GritScaleZeroVsOne_ProducesDifferentSampleBuffers)
{
	// ARRANGE: irregular values off every quantization lattice, so neither
	// grit setting can coincidentally match the other.
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.base_pitch.cents = 37;
	program_storage.voice_storage.at(0).gain = 0.6789f;
	program.filter.cutoff_hz = 5000.37f;
	program.filter.resonance_q = 3.14159f;

	const auto render_at_scale = [&](float grit_scale)
	{
		glirps::impl::Synth synth(1, 4);
		synth.configure(test_sample_rate_hz_synth);
		synth.setGritScale(grit_scale);
		auto handle = synth.start(program, 0);
		EXPECT_TRUE(handle.has_value());

		std::array<glirps::ParameterBlock, 1> blocks;
		EXPECT_EQ(synth.evaluateTick(*handle, blocks), 1u);
		synth.applyBlock(blocks.at(0));

		std::vector<float> output(frames_per_base_tick_synth);
		synth.render(output);
		return output;
	};

	// ACT
	const std::vector<float> at_scale_zero = render_at_scale(0.0f);
	const std::vector<float> at_scale_one = render_at_scale(1.0f);

	// ASSERT
	EXPECT_NE(at_scale_zero, at_scale_one);
}

// A one-shot sound (`gate.release_subtick` present) gates off once its
// authored release time has passed; a sustained sound
// (`gate.release_subtick` absent) does not.
TEST(SynthTests, EvaluateTick_OneShotReleaseTick_GatesOffAfterAuthoredCount_SustainedDoesNot)
{
	// ARRANGE
	constexpr std::uint32_t release_tick = 3;
	glirps::test_support::TestProgramStorage one_shot_storage;
	auto one_shot = sustainedTriangleProgram(one_shot_storage);
	one_shot.gate.has_release_subtick = true;
	one_shot.gate.release_subtick = release_tick * glirps::subticks_per_base_tick;
	glirps::impl::Synth one_shot_synth(1, 4);
	one_shot_synth.configure(test_sample_rate_hz_synth);
	auto one_shot_handle = one_shot_synth.start(one_shot, 0);
	ASSERT_TRUE(one_shot_handle.has_value());

	glirps::impl::Synth sustained_synth(1, 4);
	sustained_synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage sustained_storage;
	const auto sustained = sustainedTriangleProgram(sustained_storage);
	auto sustained_handle = sustained_synth.start(sustained, 0);
	ASSERT_TRUE(sustained_handle.has_value());

	// ACT: advance both instances one tick past the one-shot's authored
	// release tick.
	std::array<glirps::ParameterBlock, 1> one_shot_blocks;
	std::array<glirps::ParameterBlock, 1> sustained_blocks;
	for (std::uint32_t t = 0; t < release_tick + 1; ++t)
	{
		ASSERT_EQ(one_shot_synth.evaluateTick(*one_shot_handle, one_shot_blocks), 1u);
		ASSERT_EQ(sustained_synth.evaluateTick(*sustained_handle, sustained_blocks), 1u);
	}

	// ASSERT
	EXPECT_FALSE(one_shot_blocks.at(0).voices.at(0).gate_on);
	EXPECT_TRUE(sustained_blocks.at(0).voices.at(0).gate_on);
}

// A one-shot's `release_subtick` is a time, independent of the multiplier:
// with a `tick_multiplier` of 4, a release time of 6 base ticks gates on
// every block of the first 6 base ticks and off from the seventh: block
// times are compared against program times exactly, in subticks.
TEST(SynthTests, EvaluateTick_OneShotReleaseSubtick_WithMultiplier_ReleasesAtTime)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage one_shot_storage;
	auto one_shot = sustainedTriangleProgram(one_shot_storage);
	one_shot.gate.has_release_subtick = true;
	one_shot.gate.release_subtick = 6 * glirps::subticks_per_base_tick;
	one_shot.grit.tick_multiplier = 4;
	auto handle = synth.start(one_shot, 0);
	ASSERT_TRUE(handle.has_value());

	// ACT: seven base ticks emit effective ticks 1 to 28.
	constexpr std::uint32_t base_ticks = 7;
	constexpr std::uint32_t blocks_per_tick = 4;
	std::vector<glirps::ParameterBlock> blocks(blocks_per_tick);
	std::vector<bool> gates;
	for (std::uint32_t tick = 0; tick < base_ticks; ++tick)
	{
		const std::uint32_t count = synth.evaluateTick(*handle, blocks);
		for (const glirps::ParameterBlock& block : std::span(blocks).first(count))
			gates.push_back(block.voices.front().gate_on);
	}

	// ASSERT: the first 6 base ticks (24 blocks) gate on, the seventh off.
	std::vector<bool> expected(6 * blocks_per_tick, true);
	expected.resize(base_ticks * blocks_per_tick, false);
	EXPECT_EQ(gates, expected);
}

// A sustained instance (no `gate.release_subtick`) holds its gate across many
// ticks with no `setGate` call, and releases at the next tick once one
// arrives; `setGate` has no effect on a one-shot instance, whose gate
// releases at its authored time.
TEST(SynthTests, SetGate_SustainedInstance_HoldsUntilCalledThenReleases_OneShotUnaffected)
{
	// ARRANGE
	glirps::impl::Synth synth(2, 8);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage sustained_storage;
	const auto sustained = sustainedTriangleProgram(sustained_storage);
	auto sustained_handle = synth.start(sustained, 0);
	ASSERT_TRUE(sustained_handle.has_value());
	glirps::test_support::TestProgramStorage one_shot_storage;
	auto one_shot = sustainedTriangleProgram(one_shot_storage);
	// Held on for the whole test: far past every tick this test advances.
	one_shot.gate.has_release_subtick = true;
	one_shot.gate.release_subtick = 1000 * glirps::subticks_per_base_tick;
	auto one_shot_handle = synth.start(one_shot, 0);
	ASSERT_TRUE(one_shot_handle.has_value());

	std::array<glirps::ParameterBlock, 1> blocks;

	// ACT & ASSERT: with no `setGate` call, the sustained instance stays gated on.
	constexpr std::uint32_t held_ticks = 50;
	for (std::uint32_t t = 0; t < held_ticks; ++t)
	{
		ASSERT_EQ(synth.evaluateTick(*sustained_handle, blocks), 1u);
		ASSERT_TRUE(blocks.at(0).voices.at(0).gate_on);
	}

	// `setGate(false)` on the one-shot instance changes nothing.
	synth.setGate(*one_shot_handle, false);
	ASSERT_EQ(synth.evaluateTick(*one_shot_handle, blocks), 1u);
	EXPECT_TRUE(blocks.at(0).voices.at(0).gate_on);

	// `setGate(false)` on the sustained instance releases it at the next
	// tick.
	synth.setGate(*sustained_handle, false);
	ASSERT_EQ(synth.evaluateTick(*sustained_handle, blocks), 1u);
	EXPECT_FALSE(blocks.at(0).voices.at(0).gate_on);
}

// An instance reports `isComplete` once its last voice envelope has
// finished decaying through release, and not before: completion is
// reported per chip instance, not per voice.
TEST(SynthTests, IsComplete_TrueOnceLastVoiceEnvelopeFinishes_NotBefore)
{
	// ARRANGE: a sustained instance with the fastest legal attack, decay,
	// and release, so decay reaches a nonzero sustain level quickly and
	// release finishes within one render call.
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program_storage.voice_storage.at(0).envelope = glirps::EnvelopeSpec{.attack_seconds = 0.002f,
		.decay_seconds = 0.006f,
		.sustain_level = 0.5f,
		.release_seconds = 0.006f};
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	// ACT & ASSERT: rendering through attack, decay, and into sustain, the
	// instance is not complete. The frame count is a whole number of
	// sub-blocks, since `render` consumes whole sub-blocks.
	std::vector<float> sustaining(75 * glirps::sub_block_frame_count);
	synth.render(sustaining);
	EXPECT_FALSE(synth.isComplete(*handle));

	// ACT: release the gate through the control path, then render well past
	// the release tail. `render` keeps reapplying the last block `applyBlock`
	// installed, so `setGate` only reaches the renderer once `evaluateTick` and
	// `applyBlock` run again, since `applyBlock` is the only render-side entry
	// point that changes an instance's parameters. The frame count is a whole
	// number of sub-blocks, since `render` consumes whole sub-blocks.
	synth.setGate(*handle, false);
	std::array<glirps::ParameterBlock, 1> blocks;
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
	synth.applyBlock(blocks.at(0));
	std::vector<float> releasing(180 * glirps::sub_block_frame_count);
	synth.render(releasing);

	// ASSERT
	EXPECT_TRUE(synth.isComplete(*handle));
}

// A per-voice `gate` table value can hold a voice silent-and-released between
// retriggers while the chip-wide gate is still open (gate-table retrigger AND semantics):
// with `sustain_level: 0` and a fast decay, the envelope alone reaches
// `release`-and-silence well before the per-voice gate table's own false
// value, so `Synth::isComplete` must not report true merely because
// `ChipInstance::isComplete()` (per-voice envelope only) does: the
// chip-wide gate has not yet closed, and this same voice is retriggered a
// tick later.
TEST(SynthTests, IsComplete_PerVoiceGateTableRetrigger_NotCompleteUntilChipWideGateCloses)
{
	// A one-shot sound whose chip-wide gate stays open through tick 20; the
	// per-voice gate table holds true for ticks 1-5, false at tick 6, then
	// true again through tick 20, retriggering the envelope.
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.gate.has_release_subtick = true;
	program.gate.release_subtick = 20 * glirps::subticks_per_base_tick;
	program_storage.voice_storage.at(0).envelope = glirps::EnvelopeSpec{.attack_seconds = 0.002f,
		.decay_seconds = 0.006f,
		.sustain_level = 0.0f,
		.release_seconds = 0.006f};
	std::vector<bool> gate_rows(20, true);
	gate_rows.at(5) = false;
	program_storage.voice_storage.at(0).tables.has_gate = true;
	program_storage.voice_storage.at(0).tables.gate
		= glirps::test_support::makeBoolTable(program_storage, gate_rows, 0);
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	// ACT: render through the per-voice gate's momentary false value (tick 6)
	// and its retrigger (tick 7); the envelope decayed to silence well
	// before tick 6, per the fast decay above, so this is exactly the case
	// that must not report complete.
	for (int t = 0; t < 7; ++t)
	{
		std::array<glirps::ParameterBlock, 1> blocks;
		ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
		synth.applyBlock(blocks.at(0));
		std::vector<float> output(frames_per_base_tick_synth);
		synth.render(output);
	}

	// ASSERT
	EXPECT_FALSE(synth.isComplete(*handle));

	// ACT: render through the chip-wide gate's own release (tick 20) and
	// well past its own release tail.
	for (int t = 7; t < 30; ++t)
	{
		std::array<glirps::ParameterBlock, 1> blocks;
		ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
		synth.applyBlock(blocks.at(0));
		std::vector<float> output(frames_per_base_tick_synth);
		synth.render(output);
	}

	// ASSERT
	EXPECT_TRUE(synth.isComplete(*handle));
}

namespace
{
	// Renders one noise voice's first four sub-blocks at `seed`. Noise is
	// the one waveform whose samples depend on the seed at all: an
	// oscillator-based waveform's phase does not, so a seed comparison needs
	// it to be meaningful.
	std::vector<float> renderNoiseAtSeed(std::uint64_t seed)
	{
		glirps::impl::Synth synth(1, 4);
		synth.configure(test_sample_rate_hz_synth);
		glirps::test_support::TestProgramStorage program_storage;
		auto program = sustainedTriangleProgram(program_storage);
		program_storage.voice_storage.at(0).waveform = glirps::Waveform::noise;
		auto handle = synth.start(program, seed);
		std::vector<float> output(4 * glirps::sub_block_frame_count);
		synth.render(output);
		return output;
	}
}

// Two renders of one `SoundProgram` at one instance seed, within one
// process, are byte-identical; two different seeds produce different sample
// buffers, since every noise register and random draw is seeded per
// instance.
TEST(SynthTests, TwoRendersAtOneSeedAreIdentical_DifferentSeedsDiffer)
{
	// ARRANGE
	constexpr std::uint64_t seed_a = 123;
	constexpr std::uint64_t seed_b = 456;

	// ACT
	const std::vector<float> first_at_seed_a = renderNoiseAtSeed(seed_a);
	const std::vector<float> second_at_seed_a = renderNoiseAtSeed(seed_a);
	const std::vector<float> at_seed_b = renderNoiseAtSeed(seed_b);

	// ASSERT
	EXPECT_EQ(first_at_seed_a, second_at_seed_a);
	EXPECT_NE(first_at_seed_a, at_seed_b);
}

// A released slot handed to a new instance renders byte-identically to a
// fresh `Synth` starting the same program at the same seed: no oscillator
// phase, envelope phase or level, filter integrator, decimator history, or
// noise-register state from the previous occupant may survive, or two
// renders at one seed would differ.
TEST(SynthTests, ReusedSlotRendersIdenticallyToFreshInstance)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program_storage.voice_storage.at(0).waveform = glirps::Waveform::noise;
	auto handle = synth.start(program, 42);
	ASSERT_TRUE(handle.has_value());
	std::vector<float> first(4 * glirps::sub_block_frame_count);
	synth.render(first);
	synth.release(*handle);
	std::vector<float> acknowledge_release(glirps::sub_block_frame_count, 0.0f);
	synth.render(acknowledge_release);

	// ACT
	auto second_handle = synth.start(program, 42);
	ASSERT_TRUE(second_handle.has_value());
	std::vector<float> second(4 * glirps::sub_block_frame_count);
	synth.render(second);

	// ASSERT
	EXPECT_EQ(first, second);
}

// A slot released after a four-voice program and reused for a one-voice
// program must not keep the old voices alive: the reused render must match a
// fresh render, and the slot must complete once the one-voice program's own
// envelope ends.
TEST(SynthTests, ReusedSlotWithFewerVoicesMatchesFreshRenderAndCompletes)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 32);
	synth.configure(test_sample_rate_hz_synth);

	glirps::test_support::TestProgramStorage four_voice_storage;
	auto four_voice = sustainedTriangleProgram(four_voice_storage);
	std::vector<glirps::VoiceProgram> four_voices{four_voice_storage.voice_storage.at(0)};
	for (std::uint32_t i = 1; i < 4; ++i)
	{
		auto extra = four_voice_storage.voice_storage.at(0);
		extra.envelope = glirps::EnvelopeSpec{.attack_seconds = 0.002f,
			.decay_seconds = 24.0f,  // still decaying long after the 1-voice ends
			.sustain_level = 0.0f,
			.release_seconds = 24.0f};
		four_voices.push_back(extra);
	}
	glirps::test_support::setVoices(four_voice_storage, four_voice, four_voices);
	auto wide = synth.start(four_voice, 1);
	ASSERT_TRUE(wide.has_value());
	std::vector<float> discarded(2 * glirps::sub_block_frame_count);
	synth.render(discarded);
	synth.release(*wide);
	std::vector<float> acknowledge_release(glirps::sub_block_frame_count, 0.0f);
	synth.render(acknowledge_release);

	glirps::test_support::TestProgramStorage one_voice_storage;
	auto one_voice = sustainedTriangleProgram(one_voice_storage);
	one_voice_storage.voice_storage.at(0).envelope = glirps::EnvelopeSpec{.attack_seconds = 0.002f,
		.decay_seconds = 0.002f,
		.sustain_level = 0.0f,
		.release_seconds = 0.002f};
	auto narrow = synth.start(one_voice, 1);
	ASSERT_TRUE(narrow.has_value());
	std::vector<float> reused(4 * glirps::sub_block_frame_count);
	synth.render(reused);

	glirps::impl::Synth fresh_synth(1, 32);
	fresh_synth.configure(test_sample_rate_hz_synth);
	auto fresh_handle = fresh_synth.start(one_voice, 1);
	ASSERT_TRUE(fresh_handle.has_value());
	std::vector<float> fresh(4 * glirps::sub_block_frame_count);
	fresh_synth.render(fresh);

	// ACT
	// The reused render must equal the fresh render with no stale voice
	// leaking audio, and after the gate is released the slot must complete.
	synth.setGate(*narrow, false);
	std::vector<glirps::ParameterBlock> release_blocks(synth.getBlocksPerBaseTick(*narrow));
	std::ignore = synth.evaluateTick(*narrow, release_blocks);
	synth.applyBlock(release_blocks.at(0));
	std::vector<float> tail(2 * glirps::sub_block_frame_count);
	synth.render(tail);

	// ASSERT
	EXPECT_EQ(reused, fresh);
	EXPECT_TRUE(synth.isComplete(*narrow));
}

namespace
{
	// The first base tick's voice 0 frequency for `program` started at
	// `seed`, at full float resolution (grit scale 0 is the identity
	// transform), so the result is comparable to
	// `frequencyHertz` bit for bit with no quantization noise.
	float firstTickFrequencyHz(const glirps::SoundProgram& program, std::uint64_t seed)
	{
		glirps::impl::Synth synth(1, 4);
		synth.configure(test_sample_rate_hz_synth);
		synth.setGritScale(0.0f);
		auto handle = synth.start(program, seed);
		std::array<glirps::ParameterBlock, 1> blocks;
		std::ignore = synth.evaluateTick(*handle, blocks);
		return blocks.at(0).voices.at(0).frequency_hz;
	}
}

// Seeded pitch variation lands only on declared `scale` degrees across 100
// seeds, does not land on the same degree for all of them, and a
// `SoundProgram` with an empty `scale` produces the identical pitch across
// all 100 seeds, since an empty `scale` applies no pitch variation.
TEST(SynthTests, SeededPitchVariationSnapsToDeclaredScaleDegreesAcrossSeeds)
{
	// ARRANGE
	constexpr std::uint32_t seed_count = 100;
	const std::vector<std::int32_t> scale{0, 3, 5, 7, 10};

	glirps::test_support::TestProgramStorage with_scale_storage;
	auto with_scale = sustainedTriangleProgram(with_scale_storage);
	glirps::test_support::setScale(with_scale_storage, with_scale, scale);

	std::set<float> declared_degree_frequencies;
	for (const std::int32_t degree : scale)
		declared_degree_frequencies.insert(glirps::frequencyHertz(with_scale.base_pitch.semitone + degree, 0.0f));

	// ACT & ASSERT: every seed lands on a declared degree, and the three
	// drawn steps (-1, 0, +1) map to three distinct degrees. With the
	// cyclically wrapped index the reachable deltas for this scale are
	// {0, +3, +10}, so the full set is exactly three frequencies.
	std::set<float> observed_frequencies;
	for (std::uint32_t seed = 0; seed < seed_count; ++seed)
	{
		const float frequency = firstTickFrequencyHz(with_scale, seed);
		EXPECT_TRUE(declared_degree_frequencies.contains(frequency)) << "seed " << seed;
		observed_frequencies.insert(frequency);
	}
	EXPECT_EQ(observed_frequencies.size(), 3u);

	// An empty `scale` produces the identical, unvaried pitch for every seed.
	glirps::test_support::TestProgramStorage without_scale_storage;
	auto without_scale = sustainedTriangleProgram(without_scale_storage);
	const float unvaried_frequency = glirps::frequencyHertz(without_scale.base_pitch.semitone, 0.0f);
	for (std::uint32_t seed = 0; seed < seed_count; ++seed)
		EXPECT_FLOAT_EQ(firstTickFrequencyHz(without_scale, seed), unvaried_frequency);
}

// `start` rejects a loop start past the table's end.
TEST(SynthTests, Start_LoopStartPastTableEnd_Rejected)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program_storage.voice_storage.at(0).tables.has_gain = true;
	program_storage.voice_storage.at(0).tables.gain
		= glirps::test_support::makeFloatTable(program_storage, {1.0f, 0.5f}, 5);

	// ACT
	auto handle = synth.start(program, 0);

	// ASSERT
	ASSERT_FALSE(handle.has_value());
	EXPECT_EQ(handle.error(), glirps::StartError::invalid_program);
}

// A huge `arpeggio` value must not overflow the pitch assembly's signed 32-bit
// addition: the summed semitones are computed in 64 bits and clamped to the
// Pitch range, so the resulting frequency is finite and clamped to Nyquist.
TEST(SynthTests, EvaluateTick_HugeArpeggioRow_ProducesDefinedClampedFrequency)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program_storage.voice_storage.at(0).tables.has_arpeggio = true;
	program_storage.voice_storage.at(0).tables.arpeggio
		= glirps::test_support::makeIntTable(program_storage, {std::numeric_limits<std::int32_t>::max()}, 0);
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	// ACT
	std::array<glirps::ParameterBlock, 1> blocks;
	std::ignore = synth.evaluateTick(*handle, blocks);

	// ASSERT
	const float frequency = blocks.at(0).voices.at(0).frequency_hz;
	EXPECT_TRUE(std::isfinite(frequency));
	EXPECT_LE(frequency, static_cast<float>(test_sample_rate_hz_synth) * 0.5f);
}

// `setGritScale` clamps its argument to [0, 1] and treats a non-finite value
// as the identity transform, so a `.nan` or a huge value cannot poison every
// quantized field of every block.
TEST(SynthTests, SetGritScale_NonFiniteAndOutOfRange_AreClamped)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program_storage.voice_storage.at(0).waveform = glirps::Waveform::noise;
	std::array<glirps::ParameterBlock, 1> blocks;

	// ACT
	synth.setGritScale(std::numeric_limits<float>::quiet_NaN());
	auto nan_handle = synth.start(program, 1);
	ASSERT_TRUE(nan_handle.has_value());
	std::ignore = synth.evaluateTick(*nan_handle, blocks);

	// ASSERT
	EXPECT_TRUE(std::isfinite(blocks.at(0).output_gain));
	EXPECT_TRUE(std::isfinite(blocks.at(0).voices.at(0).gain));

	// A scale past 1 is clamped to 1 rather than allowed to floor every
	// parameter to its minimum.
	synth.release(*nan_handle);
	std::vector<float> acknowledge_release(glirps::sub_block_frame_count, 0.0f);
	synth.render(acknowledge_release);
	synth.setGritScale(1000.0f);
	auto huge_handle = synth.start(program, 1);
	ASSERT_TRUE(huge_handle.has_value());
	std::ignore = synth.evaluateTick(*huge_handle, blocks);
	EXPECT_TRUE(std::isfinite(blocks.at(0).output_gain));
	EXPECT_GE(blocks.at(0).output_gain, 0.0f);
}

// `Synth::configure(sample_rate_hz, oversampling_factor)` honors the factor:
// the default-factor overload and an explicit default render identically, and
// a different factor changes the rendered samples.
TEST(SynthTests, Configure_OversamplingFactor_IsHonored)
{
	// ARRANGE
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program_storage.voice_storage.at(0).waveform = glirps::Waveform::noise;

	glirps::impl::Synth default_synth(1, 4);
	default_synth.configure(test_sample_rate_hz_synth);
	auto default_handle = default_synth.start(program, 7);
	ASSERT_TRUE(default_handle.has_value());
	std::vector<float> default_out(4 * glirps::sub_block_frame_count);
	default_synth.render(default_out);

	glirps::impl::Synth explicit_synth(1, 4);
	explicit_synth.configure(test_sample_rate_hz_synth, glirps::default_oversampling_factor);
	auto explicit_handle = explicit_synth.start(program, 7);
	ASSERT_TRUE(explicit_handle.has_value());
	std::vector<float> explicit_out(4 * glirps::sub_block_frame_count);
	explicit_synth.render(explicit_out);

	glirps::impl::Synth low_synth(1, 4);
	low_synth.configure(test_sample_rate_hz_synth, 1);
	auto low_handle = low_synth.start(program, 7);
	ASSERT_TRUE(low_handle.has_value());
	std::vector<float> low_out(4 * glirps::sub_block_frame_count);
	low_synth.render(low_out);

	// ACT & ASSERT
	EXPECT_EQ(default_out, explicit_out);
	EXPECT_NE(default_out, low_out);
}

// `start` rejects a zero or out-of-range `tick_multiplier`.
TEST(SynthTests, Start_RejectsOutOfRangeTickMultiplier)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);

	// ACT & ASSERT
	glirps::test_support::TestProgramStorage zero_storage;
	auto zero = sustainedTriangleProgram(zero_storage);
	zero.grit.tick_multiplier = 0;
	auto zero_handle = synth.start(zero, 0);
	ASSERT_FALSE(zero_handle.has_value());
	EXPECT_EQ(zero_handle.error(), glirps::StartError::invalid_program);

	glirps::test_support::TestProgramStorage huge_storage;
	auto huge = sustainedTriangleProgram(huge_storage);
	huge.grit.tick_multiplier = 17;
	auto huge_handle = synth.start(huge, 0);
	ASSERT_FALSE(huge_handle.has_value());
	EXPECT_EQ(huge_handle.error(), glirps::StartError::invalid_program);
}

// `start` rejects an out-of-range grit bit depth.
TEST(SynthTests, Start_RejectsOutOfRangeBitDepth)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);

	// ACT
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.grit.bit_depths.pitch = 64;
	auto handle = synth.start(program, 0);

	// ASSERT
	ASSERT_FALSE(handle.has_value());
	EXPECT_EQ(handle.error(), glirps::StartError::invalid_program);
}

// `start` rejects inverted or out-of-range `pulse_width_delta_bounds`.
TEST(SynthTests, Start_RejectsInvalidAccumulateBounds_Inverted)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage inverted_storage;
	auto inverted = sustainedTriangleProgram(inverted_storage);
	inverted_storage.voice_storage.at(0).tables.pulse_width_delta_bounds
		= glirps::AccumulateBounds{.has_min = true, .min = 0.8f, .has_max = true, .max = 0.2f};

	// ACT
	auto inverted_handle = synth.start(inverted, 0);

	// ASSERT
	ASSERT_FALSE(inverted_handle.has_value());
	EXPECT_EQ(inverted_handle.error(), glirps::StartError::invalid_program);
}

TEST(SynthTests, Start_RejectsInvalidAccumulateBounds_OutOfRange)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage out_of_range_storage;
	auto out_of_range = sustainedTriangleProgram(out_of_range_storage);
	out_of_range_storage.voice_storage.at(0).tables.pulse_width_delta_bounds
		= glirps::AccumulateBounds{.has_min = true, .min = 2.0f};

	// ACT
	auto out_of_range_handle = synth.start(out_of_range, 0);

	// ASSERT
	ASSERT_FALSE(out_of_range_handle.has_value());
	EXPECT_EQ(out_of_range_handle.error(), glirps::StartError::invalid_program);
}

// `start` rejects a table value whose `max` is below its `min`.
TEST(SynthTests, Start_InvertedValueRange_Rejected)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program_storage.voice_storage.at(0).tables.has_gain = true;
	program_storage.voice_storage.at(0).tables.gain = glirps::test_support::makeFloatTable(program_storage,
		{1.0f},
		0,
		1,
		{glirps::ValueRange<float>{.min = 0.8f, .max = 0.2f}});

	// ACT
	auto handle = synth.start(program, 0);

	// ASSERT
	ASSERT_FALSE(handle.has_value());
	EXPECT_EQ(handle.error(), glirps::StartError::invalid_program);
}

// `start` rejects a routing source that is out of range or names its own voice.
TEST(SynthTests, Start_RejectsInvalidRoutingSource_SelfReferential)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage self_referential_storage;
	auto self_referential = sustainedTriangleProgram(self_referential_storage);
	self_referential_storage.voice_storage.at(0).has_ring_mod_source = true;
	self_referential_storage.voice_storage.at(0).ring_mod_source = 0;

	// ACT
	auto self_referential_handle = synth.start(self_referential, 0);

	// ASSERT
	ASSERT_FALSE(self_referential_handle.has_value());
	EXPECT_EQ(self_referential_handle.error(), glirps::StartError::invalid_program);
}

TEST(SynthTests, Start_RejectsInvalidRoutingSource_OutOfRange)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage out_of_range_storage;
	auto out_of_range = sustainedTriangleProgram(out_of_range_storage);
	out_of_range_storage.voice_storage.at(0).has_hard_sync_source = true;
	out_of_range_storage.voice_storage.at(0).hard_sync_source = 5;

	// ACT
	auto out_of_range_handle = synth.start(out_of_range, 0);

	// ASSERT
	ASSERT_FALSE(out_of_range_handle.has_value());
	EXPECT_EQ(out_of_range_handle.error(), glirps::StartError::invalid_program);
}

// `Synth::start` applies the authored ring-mod/hard-sync routing to the
// renderer before the instance's first `evaluateTick`, so a two-voice
// program declaring the routing renders audibly differently than the same
// program with no routing.
TEST(SynthTests, Render_RingModSourceRouting_ProducesDifferentOutputThanUnrouted)
{
	// ARRANGE
	const auto build = [](bool routed)
	{
		glirps::test_support::TestProgramStorage program_storage;
		program_storage.program.base_pitch = glirps::Pitch{.semitone = 36, .cents = 0};
		glirps::VoiceProgram carrier;
		carrier.waveform = glirps::Waveform::triangle;
		carrier.gain = 1.0f;
		carrier.envelope = glirps::EnvelopeSpec{.attack_seconds = 0.002f,
			.decay_seconds = 0.006f,
			.sustain_level = 1.0f,
			.release_seconds = 0.006f};
		glirps::VoiceProgram modulator = carrier;
		if (routed)
		{
			carrier.has_ring_mod_source = true;
			carrier.ring_mod_source = 1;
		}
		glirps::test_support::setVoices(program_storage, {carrier, modulator});
		return program_storage;
	};

	// ACT
	std::vector<float> unrouted_output;
	auto unrouted_storage = build(false);
	ASSERT_TRUE(renderThroughSynth(unrouted_storage.program, 256, unrouted_output));
	std::vector<float> routed_output;
	auto routed_storage = build(true);
	ASSERT_TRUE(renderThroughSynth(routed_storage.program, 256, routed_output));

	// ASSERT
	EXPECT_NE(unrouted_output, routed_output);
}

// The modulator runs an octave above the carrier, because hard sync between
// equal frequencies and phases changes nothing. The arpeggio offset needs an
// evaluated and applied tick.
TEST(SynthTests, Render_HardSyncSourceRouting_ProducesDifferentOutputThanUnrouted)
{
	// ARRANGE
	const auto build = [](bool routed)
	{
		glirps::test_support::TestProgramStorage program_storage;
		program_storage.program.base_pitch = glirps::Pitch{.semitone = 36, .cents = 0};
		glirps::VoiceProgram carrier;
		carrier.waveform = glirps::Waveform::triangle;
		carrier.gain = 1.0f;
		carrier.envelope = glirps::EnvelopeSpec{.attack_seconds = 0.002f,
			.decay_seconds = 0.006f,
			.sustain_level = 1.0f,
			.release_seconds = 0.006f};
		glirps::VoiceProgram modulator = carrier;
		modulator.tables.has_arpeggio = true;
		modulator.tables.arpeggio = glirps::test_support::makeIntTable(program_storage, {12}, 0);
		if (routed)
		{
			carrier.has_hard_sync_source = true;
			carrier.hard_sync_source = 1;
		}
		glirps::test_support::setVoices(program_storage, {carrier, modulator});
		return program_storage;
	};
	const auto render_one_tick = [](const glirps::SoundProgram& program, std::vector<float>& output)
	{
		glirps::impl::Synth synth(1, 4);
		synth.configure(test_sample_rate_hz_synth);
		auto handle = synth.start(program, 0);
		ASSERT_TRUE(handle.has_value());
		std::array<glirps::ParameterBlock, 1> block;
		ASSERT_EQ(synth.evaluateTick(*handle, block), 1u);
		synth.applyBlock(block.at(0));
		output.assign(frames_per_base_tick_synth, 0.0f);
		synth.render(output);
	};

	// ACT
	std::vector<float> unrouted_output;
	auto unrouted_storage = build(false);
	render_one_tick(unrouted_storage.program, unrouted_output);
	std::vector<float> routed_output;
	auto routed_storage = build(true);
	render_one_tick(routed_storage.program, routed_output);

	// ASSERT
	EXPECT_NE(unrouted_output, routed_output);
}

// A `step` segment's interval sets how long each value holds: a two-tick
// interval advances at half the rate of a one-tick interval.
TEST(SynthTests, EvaluateTick_StepIntervalTwoTicks_AdvancesAtHalfTheRateOfOneTick)
{
	// ARRANGE
	const auto build = [](std::uint32_t ticks_per_value)
	{
		glirps::test_support::TestProgramStorage program_storage;
		auto program = sustainedTriangleProgram(program_storage);
		program_storage.voice_storage.at(0).tables.has_arpeggio = true;
		program_storage.voice_storage.at(0).tables.arpeggio
			= glirps::test_support::makeIntTable(program_storage, {0, 12}, 1, ticks_per_value);
		return program_storage;
	};
	auto storage_rate1 = build(1);
	glirps::impl::Synth synth_rate1(1, 4);
	synth_rate1.configure(test_sample_rate_hz_synth);
	synth_rate1.setGritScale(0.0f);
	auto handle_rate1 = synth_rate1.start(storage_rate1.program, 0);
	ASSERT_TRUE(handle_rate1.has_value());
	auto storage_rate2 = build(2);
	glirps::impl::Synth synth_rate2(1, 4);
	synth_rate2.configure(test_sample_rate_hz_synth);
	synth_rate2.setGritScale(0.0f);
	auto handle_rate2 = synth_rate2.start(storage_rate2.program, 0);
	ASSERT_TRUE(handle_rate2.has_value());

	// ACT
	std::array<glirps::ParameterBlock, 1> block_rate1_tick1;
	ASSERT_EQ(synth_rate1.evaluateTick(*handle_rate1, block_rate1_tick1), 1u);
	std::array<glirps::ParameterBlock, 1> block_rate1_tick2;
	ASSERT_EQ(synth_rate1.evaluateTick(*handle_rate1, block_rate1_tick2), 1u);
	std::array<glirps::ParameterBlock, 1> block_rate2_tick1;
	ASSERT_EQ(synth_rate2.evaluateTick(*handle_rate2, block_rate2_tick1), 1u);
	std::array<glirps::ParameterBlock, 1> block_rate2_tick2;
	ASSERT_EQ(synth_rate2.evaluateTick(*handle_rate2, block_rate2_tick2), 1u);

	// ASSERT
	// A one-tick interval already advanced onto value 1 (+12 semitones) at
	// its second tick.
	EXPECT_GT(block_rate1_tick2.at(0).voices.at(0).frequency_hz, block_rate1_tick1.at(0).voices.at(0).frequency_hz);
	// A two-tick interval is still holding value 0 at its second tick: half
	// the rate means the value has not yet advanced.
	EXPECT_FLOAT_EQ(block_rate2_tick2.at(0).voices.at(0).frequency_hz,
		block_rate2_tick1.at(0).voices.at(0).frequency_hz);
}

// `pitch_slide_bounds` narrows `pitch_slide`'s accumulator clamp beyond the
// runtime frequency clamp: a slide that would otherwise walk arbitrarily far
// is held to the authored `[min, max]`.
TEST(SynthTests, EvaluateTick_PitchSlideBounds_ClampsAccumulatorNarrowerThanUnbounded)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program_storage.voice_storage.at(0).tables.has_pitch_slide = true;
	program_storage.voice_storage.at(0).tables.pitch_slide
		= glirps::test_support::makeFloatTable(program_storage, {-50.0f}, 0);
	program_storage.voice_storage.at(0).tables.pitch_slide_bounds
		= glirps::AccumulateBounds{.has_min = true, .min = -80.0f};
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	// ACT
	// Enough ticks that an unbounded accumulator would have walked far past
	// -80 cents.
	float last_frequency = -1.0f;
	for (int t = 0; t < 10; ++t)
	{
		std::array<glirps::ParameterBlock, 1> blocks;
		ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
		last_frequency = blocks.at(0).voices.at(0).frequency_hz;
	}

	// ASSERT
	// The accumulator holds at -80 cents rather than the -500 cents an
	// unbounded slide would reach.
	const float expected = glirps::frequencyHertz(36, -80.0f);
	EXPECT_NEAR(last_frequency, expected, 0.01f);
}

// `pulse_width_delta_bounds` narrows the `[0, 1]` runtime clamp further:
// authoring `max: 0.7` holds the accumulator there instead of the unbounded
// default's 1.0.
TEST(SynthTests, EvaluateTick_PulseWidthDeltaBounds_ClampsNarrowerThanRuntimeRange)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program_storage.voice_storage.at(0).pulse_width = 0.5f;
	program_storage.voice_storage.at(0).tables.has_pulse_width_delta = true;
	program_storage.voice_storage.at(0).tables.pulse_width_delta
		= glirps::test_support::makeFloatTable(program_storage, {0.6f}, 0);
	program_storage.voice_storage.at(0).tables.pulse_width_delta_bounds
		= glirps::AccumulateBounds{.has_max = true, .max = 0.7f};
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	// ACT
	std::array<glirps::ParameterBlock, 1> blocks;
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);

	// ASSERT
	// 0.5 + 0.6 = 1.1 would clamp to 1.0 unbounded, but the authored max of
	// 0.7 clamps it there instead.
	EXPECT_FLOAT_EQ(blocks.at(0).voices.at(0).pulse_width, 0.7f);
}

// A per-voice `gate` table value is ANDed with the chip-wide/one-shot-derived
// gate, never overriding it: while the chip-wide gate is held, a
// false-then-true value sequence gates the voice off then on again; once the
// chip-wide gate itself goes false (`release_subtick` reached), a `true` value
// cannot reopen it.
TEST(SynthTests, EvaluateTick_GateTable_AndedWithChipWideGate_NeverOverrides)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.gate.has_release_subtick = true;
	program.gate.release_subtick = 3 * glirps::subticks_per_base_tick;
	program_storage.voice_storage.at(0).tables.has_gate = true;
	program_storage.voice_storage.at(0).tables.gate
		= glirps::test_support::makeBoolTable(program_storage, {false, true, true, true, true}, 4);
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	// Tick 1's value is false, gating the voice off despite the held
	// chip-wide gate. Ticks 2 and 3's values are true, so the voice follows
	// the chip-wide gate, which is held through tick 3. Ticks 4 and 5's
	// values are true too, but the chip-wide gate itself released after
	// tick 3, so the value cannot reopen it.
	const std::array<bool, 5> expected_gate_on{false, true, true, false, false};

	// ACT
	std::array<bool, 5> actual_gate_on{};
	for (bool& gate_on : actual_gate_on)
	{
		std::array<glirps::ParameterBlock, 1> blocks;
		ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
		gate_on = blocks.at(0).voices.at(0).gate_on;
	}

	// ASSERT
	EXPECT_EQ(actual_gate_on, expected_gate_on);
}

// A voice whose `start_subtick` is nonzero holds inactive (gated off, no
// table evaluated) until a block reaches that time, then becomes active
// exactly as `start_subtick == 0` does: gated on, tables at time 0, since
// `start_subtick` is the time origin of the voice's tables.
TEST(SynthTests, EvaluateTick_StartSubtick_HoldsInactiveThenBeginsFreshAtTime)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program_storage.voice_storage.at(0).start_subtick = 2 * glirps::subticks_per_base_tick;
	program_storage.voice_storage.at(0).tables.has_arpeggio = true;
	program_storage.voice_storage.at(0).tables.arpeggio
		= glirps::test_support::makeIntTable(program_storage, {0, 4, 7}, 0);
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());

	// ACT: ticks 1 and 2, still inactive.
	std::array<bool, 2> inactive_gate_on{};
	for (bool& gate_on : inactive_gate_on)
	{
		std::array<glirps::ParameterBlock, 1> blocks;
		ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
		gate_on = blocks.at(0).voices.at(0).gate_on;
	}

	// ASSERT: gated off, and the arpeggio has not advanced past value 0 once
	// the voice starts.
	for (const bool gate_on : inactive_gate_on)
		EXPECT_FALSE(gate_on);

	// ACT: tick 3, the voice becomes active.
	std::array<glirps::ParameterBlock, 1> start_blocks;
	ASSERT_EQ(synth.evaluateTick(*handle, start_blocks), 1u);

	// ASSERT: gated on, at arpeggio value 0 (offset 0) exactly as a
	// `start_subtick == 0` voice would be on its first tick.
	EXPECT_TRUE(start_blocks.at(0).voices.at(0).gate_on);
	EXPECT_NEAR(start_blocks.at(0).voices.at(0).frequency_hz, glirps::frequencyHertz(36, 0.0f), 0.01f);

	// ACT: tick 4, normal continued advance.
	std::array<glirps::ParameterBlock, 1> next_blocks;
	ASSERT_EQ(synth.evaluateTick(*handle, next_blocks), 1u);

	// ASSERT: at arpeggio value 1 (offset 4).
	EXPECT_TRUE(next_blocks.at(0).voices.at(0).gate_on);
	EXPECT_NEAR(next_blocks.at(0).voices.at(0).frequency_hz, glirps::frequencyHertz(40, 0.0f), 0.01f);
}

// A random value draws uniformly from `[min, max]`, seeded per instance and
// redrawn on every pass of its table: a one-value table looping every tick
// draws anew each tick, two instances started at the same seed draw an
// identical sequence, and a different seed draws a different one, since
// every random value comes from a generator seeded by the instance seed.
TEST(SynthTests, EvaluateTick_RandomValue_SameSeedDeterministic_DifferentSeedsDiffer)
{
	// ARRANGE
	const auto stochastic_arpeggio_program = []
	{
		glirps::test_support::TestProgramStorage program_storage;
		auto program = sustainedTriangleProgram(program_storage);
		program_storage.voice_storage.at(0).tables.has_arpeggio = true;
		program_storage.voice_storage.at(0).tables.arpeggio = glirps::test_support::makeIntTable(program_storage,
			{0},
			0,
			1,
			{glirps::ValueRange<std::int32_t>{.min = -24, .max = 24}});
		return program_storage;
	};
	auto storage_a = stochastic_arpeggio_program();
	auto storage_b = stochastic_arpeggio_program();
	auto storage_c = stochastic_arpeggio_program();
	auto& program_a = storage_a.program;
	auto& program_b = storage_b.program;
	auto& program_c = storage_c.program;

	glirps::impl::Synth synth_a(1, 4);
	synth_a.configure(test_sample_rate_hz_synth);
	synth_a.setGritScale(0.0f);
	auto handle_a = synth_a.start(program_a, 42);
	ASSERT_TRUE(handle_a.has_value());

	glirps::impl::Synth synth_b(1, 4);
	synth_b.configure(test_sample_rate_hz_synth);
	synth_b.setGritScale(0.0f);
	auto handle_b = synth_b.start(program_b, 42);
	ASSERT_TRUE(handle_b.has_value());

	glirps::impl::Synth synth_c(1, 4);
	synth_c.configure(test_sample_rate_hz_synth);
	synth_c.setGritScale(0.0f);
	auto handle_c = synth_c.start(program_c, 99);
	ASSERT_TRUE(handle_c.has_value());

	// Draw many ticks, long enough that a different-seed sequence matching
	// by coincidence, or every draw landing back on the plain table value of
	// 0, is practically impossible.
	// ACT
	constexpr int tick_count = 20;
	std::vector<float> sequence_a;
	std::vector<float> sequence_b;
	std::vector<float> sequence_c;
	bool any_draw_left_base_pitch = false;
	const float base_frequency = glirps::frequencyHertz(36, 0.0f);
	for (int t = 0; t < tick_count; ++t)
	{
		std::array<glirps::ParameterBlock, 1> blocks_a;
		std::array<glirps::ParameterBlock, 1> blocks_b;
		std::array<glirps::ParameterBlock, 1> blocks_c;
		ASSERT_EQ(synth_a.evaluateTick(*handle_a, blocks_a), 1u);
		ASSERT_EQ(synth_b.evaluateTick(*handle_b, blocks_b), 1u);
		ASSERT_EQ(synth_c.evaluateTick(*handle_c, blocks_c), 1u);
		sequence_a.push_back(blocks_a.at(0).voices.at(0).frequency_hz);
		sequence_b.push_back(blocks_b.at(0).voices.at(0).frequency_hz);
		sequence_c.push_back(blocks_c.at(0).voices.at(0).frequency_hz);
		if (blocks_a.at(0).voices.at(0).frequency_hz != base_frequency)
			any_draw_left_base_pitch = true;
	}

	// ASSERT
	EXPECT_TRUE(any_draw_left_base_pitch);
	EXPECT_EQ(sequence_a, sequence_b);
	EXPECT_NE(sequence_a, sequence_c);
}

// An instance started with a nonzero onset delay holds every voice inactive
// (gated off, every table still at its start) until the delay elapses,
// exactly as a voice with a later `start_subtick` holds. Base ticks are
// 1-indexed in the delay, so a delay of 3 activates on the third tick
// `evaluateTick` emits, keeping onset on the base control grid.
TEST(SynthTests, EvaluateTick_OnsetDelay_HoldsInactiveThenBeginsFreshAtTheDelayedTick)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program_storage.voice_storage.at(0).tables.has_arpeggio = true;
	program_storage.voice_storage.at(0).tables.arpeggio
		= glirps::test_support::makeIntTable(program_storage, {0, 4, 7}, 0);
	auto handle = synth.start(program, 0, 3);
	ASSERT_TRUE(handle.has_value());

	// ACT: ticks 1 and 2, still inactive.
	std::array<bool, 2> inactive_gate_on{};
	for (bool& gate_on : inactive_gate_on)
	{
		std::array<glirps::ParameterBlock, 1> blocks;
		ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
		gate_on = blocks.at(0).voices.at(0).gate_on;
	}

	// ASSERT: gated off throughout the delay.
	for (const bool gate_on : inactive_gate_on)
		EXPECT_FALSE(gate_on);

	// ACT: tick 3, the delay elapses.
	std::array<glirps::ParameterBlock, 1> start_blocks;
	ASSERT_EQ(synth.evaluateTick(*handle, start_blocks), 1u);

	// ASSERT: gated on, at the arpeggio's first value (offset 0) exactly as
	// an undelayed voice is on its first tick: the table time did not run
	// while the voice was held.
	EXPECT_TRUE(start_blocks.at(0).voices.at(0).gate_on);
	EXPECT_NEAR(start_blocks.at(0).voices.at(0).frequency_hz, glirps::frequencyHertz(36, 0.0f), 0.01f);

	// ACT: tick 4, normal continued advance.
	std::array<glirps::ParameterBlock, 1> next_blocks;
	ASSERT_EQ(synth.evaluateTick(*handle, next_blocks), 1u);

	// ASSERT: at arpeggio value 1 (offset 4).
	EXPECT_TRUE(next_blocks.at(0).voices.at(0).gate_on);
	EXPECT_NEAR(next_blocks.at(0).voices.at(0).frequency_hz, glirps::frequencyHertz(40, 0.0f), 0.01f);
}

// A voice carrying both an authored `start_subtick` and an instance onset
// delay (base ticks) activates at the later of the two. Neither is silently
// dropped. The tick multiplier is 1 here, so each block is one base tick
// and the comparison is the only thing under test.
TEST(SynthTests, EvaluateTick_StartSubtickAndOnsetDelay_ActivatesAtTheLaterOfTheTwo)
{
	// ARRANGE: returns the 1-indexed tick a voice first gates on at, or 0 if
	// it never does within the window. `due_tick` is the 1-indexed tick the
	// voice is due on, so its `start_subtick` is `due_tick - 1` base ticks.
	const auto first_active_tick = [](std::uint32_t due_tick, std::uint32_t onset_delay_base_ticks) -> std::uint32_t
	{
		glirps::impl::Synth synth(1, 4);
		synth.configure(test_sample_rate_hz_synth);
		synth.setGritScale(0.0f);
		glirps::test_support::TestProgramStorage program_storage;
		auto program = sustainedTriangleProgram(program_storage);
		program_storage.voice_storage.at(0).start_subtick
			= due_tick == 0 ? 0 : (due_tick - 1) * glirps::subticks_per_base_tick;
		auto handle = synth.start(program, 0, onset_delay_base_ticks);
		if (!handle.has_value())
			return 0;
		for (std::uint32_t tick = 1; tick <= 8; ++tick)
		{
			std::array<glirps::ParameterBlock, 1> blocks;
			if (synth.evaluateTick(*handle, blocks) != 1u)
				return 0;
			if (blocks.at(0).voices.at(0).gate_on)
				return tick;
		}
		return 0;
	};

	// ACT & ASSERT
	EXPECT_EQ(first_active_tick(5, 2), 5u);
	EXPECT_EQ(first_active_tick(2, 5), 5u);
	EXPECT_EQ(first_active_tick(0, 4), 4u);
	EXPECT_EQ(first_active_tick(4, 0), 4u);
}

// An instance held by an onset delay renders silence until the block that ends
// the delay is applied.
TEST(SynthTests, Render_OnsetDelay_ProducesSilenceUntilTheDelayElapses)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	const auto program = sustainedTriangleProgram(program_storage);
	auto handle = synth.start(program, 0, 2);
	ASSERT_TRUE(handle.has_value());
	std::vector<float> block(glirps::sub_block_frame_count * 4, 0.0f);

	// ACT: before any tick, across the held tick, then across the tick the
	// delay elapses on.
	synth.render(block);
	const float before_any_tick = peakOf(block);

	std::array<glirps::ParameterBlock, 1> held;
	ASSERT_EQ(synth.evaluateTick(*handle, held), 1u);
	synth.applyBlock(held.at(0));
	std::ranges::fill(block, 0.0f);
	synth.render(block);
	const float during_delay = peakOf(block);

	std::array<glirps::ParameterBlock, 1> active;
	ASSERT_EQ(synth.evaluateTick(*handle, active), 1u);
	synth.applyBlock(active.at(0));
	std::ranges::fill(block, 0.0f);
	synth.render(block);
	const float after_delay = peakOf(block);

	// ASSERT
	EXPECT_EQ(before_any_tick, 0.0f);
	EXPECT_EQ(during_delay, 0.0f);
	EXPECT_GT(after_delay, 0.0f);
}

// A one-shot whose chip-wide gate closes before the render side has applied
// anything must not be judged complete while the gate-on block it emitted is
// still queued. The control side closes `chip_gate_open` as soon as it
// evaluates past `release_subtick`, which for a short one-shot is before the
// gate-on block is applied; latching there would reclaim the instance before
// it ever sounded. A deferred onset exposes this,
// because the gate does not open at `start`.
TEST(SynthTests, IsComplete_ShortOneShotWithDeferredOnset_StaysIncompleteUntilItsGateOnIsApplied)
{
	// ARRANGE: release on the first tick, so the second `evaluateTick` closes
	// the chip-wide gate while nothing has been applied yet.
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.gate.has_release_subtick = true;
	program.gate.release_subtick = glirps::subticks_per_base_tick;
	auto handle = synth.start(program, 0, 1);
	ASSERT_TRUE(handle.has_value());

	std::vector<float> block(glirps::sub_block_frame_count * 4, 0.0f);

	// ACT: evaluate both ticks without applying either, then render.
	std::array<glirps::ParameterBlock, 1> gate_on_blocks;
	ASSERT_EQ(synth.evaluateTick(*handle, gate_on_blocks), 1u);
	ASSERT_TRUE(gate_on_blocks.at(0).voices.at(0).gate_on);
	std::array<glirps::ParameterBlock, 1> gate_off_blocks;
	ASSERT_EQ(synth.evaluateTick(*handle, gate_off_blocks), 1u);
	ASSERT_FALSE(gate_off_blocks.at(0).voices.at(0).gate_on);
	synth.render(block);

	// ASSERT: not complete, since its gate-on has not been applied yet.
	EXPECT_FALSE(synth.isComplete(*handle));

	// ACT: the queued blocks arrive in order and the sound runs out.
	synth.applyBlock(gate_on_blocks.at(0));
	std::ranges::fill(block, 0.0f);
	synth.render(block);
	const float peak_after_gate_on = peakOf(block);

	synth.applyBlock(gate_off_blocks.at(0));
	for (int i = 0; i < 64 && !synth.isComplete(*handle); ++i)
		synth.render(block);

	// ASSERT: it sounded, and it still completes afterwards.
	EXPECT_GT(peak_after_gate_on, 0.0f);
	EXPECT_TRUE(synth.isComplete(*handle));
}

// The other side of the same trade: an instance that emits no gate-on at all
// can never sound, so it must complete rather than hold its pool slot forever
// waiting for a gate that will never arrive.
TEST(SynthTests, IsComplete_DeferredOnsetThatNeverGatesOn_CompletesRatherThanHoldingItsSlot)
{
	// ARRANGE: `release_subtick` of zero, so no emitted block ever gates a voice
	// on, and the onset delay keeps `start`'s base block gated off too.
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_synth);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage program_storage;
	auto program = sustainedTriangleProgram(program_storage);
	program.gate.has_release_subtick = true;
	program.gate.release_subtick = 0;
	auto handle = synth.start(program, 0, 1);
	ASSERT_TRUE(handle.has_value());

	// ACT
	std::array<glirps::ParameterBlock, 1> blocks;
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);
	ASSERT_FALSE(blocks.at(0).voices.at(0).gate_on);
	std::vector<float> block(glirps::sub_block_frame_count, 0.0f);
	synth.render(block);

	// ASSERT
	EXPECT_TRUE(synth.isComplete(*handle));
}
