#include "glirps/glirps.hpp"
#include "glirps/glirps_handles.hpp"

#include "SoundProgramTestSupport.hpp"
#include "SynthApiTestSupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Headless integration tests for `synthSetMixer` and `synthGetChannelCount` in
// `glirps/glirps.hpp`.
using glirps::test_support::makeSustainedProgram;
using glirps::test_support::peakOf;
using glirps::test_support::test_oversampling_factor;
using glirps::test_support::test_sample_rate_hz;

namespace
{
	// Plain context the mixer callbacks write through, so the test can observe
	// the mixer callbacks without gmock.
	struct MixerContext
	{
		std::uint32_t channel_count = 2;
		int channel_count_calls = 0;
		int begin_calls = 0;
		int submit_calls = 0;
		int end_calls = 0;
		std::uint32_t last_frame_count = 0;
		double accumulated_abs = 0.0;
	};

	std::uint32_t mixerGetChannelCount(void* context) noexcept
	{
		MixerContext& ctx = *static_cast<MixerContext*>(context);
		++ctx.channel_count_calls;
		return ctx.channel_count;
	}

	void mixerBeginBlock(void* context, glirps::shims::Span<float> output, std::uint32_t frame_count) noexcept
	{
		MixerContext& ctx = *static_cast<MixerContext*>(context);
		++ctx.begin_calls;
		ctx.last_frame_count = frame_count;
		std::ranges::fill(output, 0.0f);
	}

	void mixerSubmitInstance(void* context, glirps::ChipInstanceHandle, glirps::shims::Span<const float> mono) noexcept
	{
		MixerContext& ctx = *static_cast<MixerContext*>(context);
		++ctx.submit_calls;
		for (const float sample : mono)
			ctx.accumulated_abs += std::fabs(sample);
	}

	void mixerEndBlock(void* context) noexcept
	{
		MixerContext& ctx = *static_cast<MixerContext*>(context);
		++ctx.end_calls;
	}
}

// Installed `MixerCallbacks` fire on render and honor the callback channel
// count; passing null restores the default mono mixer; and a partially-null
// struct is rejected (a debug assertion in debug builds).
TEST(SynthMixerTests, SetMixer_Callbacks_FireHonorChannelCountAndDefaultRestores)
{
	// ARRANGE
	glirps::test_support::TestProgramStorage storage;
	const glirps::SoundProgram program = makeSustainedProgram(storage);
	const glirps::CreateResult created = glirps::synthCreate(1, 4);
	ASSERT_TRUE(created);
	const glirps::SynthOwner synth_owner(created.synth);
	const glirps::SynthHandle synth = synth_owner.get();
	glirps::synthConfigure(synth, test_sample_rate_hz, test_oversampling_factor);
	const glirps::StartResult started = glirps::synthStart(synth, program, 0);
	ASSERT_TRUE(started);

	MixerContext ctx;
	const glirps::MixerCallbacks callbacks{.context = &ctx,
		.get_channel_count = &mixerGetChannelCount,
		.begin_block = &mixerBeginBlock,
		.submit_instance = &mixerSubmitInstance,
		.end_block = &mixerEndBlock};

	// ACT: install the callbacks and render a whole number of sub-blocks,
	// interleaved at the callback channel count.
	glirps::synthSetMixer(synth, &callbacks);
	std::vector<float> output(ctx.channel_count * glirps::sub_block_frame_count, 0.0f);
	glirps::synthRender(synth, glirps::shims::toSpan(output));

	// ASSERT: the mixer callbacks ran, the channel count was queried, and the instance
	// produced non-silent audio.
	EXPECT_GT(ctx.begin_calls, 0);
	EXPECT_GT(ctx.submit_calls, 0);
	EXPECT_GT(ctx.end_calls, 0);
	EXPECT_GT(ctx.channel_count_calls, 0);
	EXPECT_EQ(ctx.last_frame_count, glirps::sub_block_frame_count);
	EXPECT_GT(ctx.accumulated_abs, 0.0);

	// ACT: a null pointer restores the default mono mixer.
	const MixerContext before_default = ctx;
	glirps::synthSetMixer(synth, nullptr);
	std::vector<float> mono_output(glirps::sub_block_frame_count, 0.0f);
	glirps::synthRender(synth, glirps::shims::toSpan(mono_output));

	// ASSERT: the custom callbacks no longer fire, and the default mono mixer
	// produces output.
	EXPECT_EQ(ctx.begin_calls, before_default.begin_calls);
	EXPECT_EQ(ctx.submit_calls, before_default.submit_calls);
	EXPECT_EQ(ctx.end_calls, before_default.end_calls);
	EXPECT_GT(peakOf(mono_output), 0.0f);

	// ACT & ASSERT: a struct with a null function pointer is rejected. In debug
	// builds the boundary's assertion fires; in release the same call falls
	// back to the default mono mixer without dying.
	glirps::MixerCallbacks partial = callbacks;
	partial.end_block = nullptr;
	EXPECT_DEBUG_DEATH(glirps::synthSetMixer(synth, &partial), "");
}

// `synthGetChannelCount` reports 1 for the default mixer and the callback's
// channel count once a mixer is installed.
TEST(SynthMixerTests, GetChannelCount_DefaultAndCallbackMixer_ReportsMixerChannels)
{
	// ARRANGE
	const glirps::CreateResult created = glirps::synthCreate(1, 4);
	ASSERT_TRUE(created);
	const glirps::SynthOwner synth_owner(created.synth);
	const glirps::SynthHandle synth = synth_owner.get();
	MixerContext ctx;
	const glirps::MixerCallbacks callbacks{.context = &ctx,
		.get_channel_count = &mixerGetChannelCount,
		.begin_block = &mixerBeginBlock,
		.submit_instance = &mixerSubmitInstance,
		.end_block = &mixerEndBlock};

	// ACT
	const std::uint32_t default_channels = glirps::synthGetChannelCount(synth);
	glirps::synthSetMixer(synth, &callbacks);
	const std::uint32_t callback_channels = glirps::synthGetChannelCount(synth);

	// ASSERT
	EXPECT_EQ(default_channels, 1U);
	EXPECT_EQ(callback_channels, ctx.channel_count);
}
