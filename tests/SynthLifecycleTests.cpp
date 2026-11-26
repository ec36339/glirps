#include "glirps/glirps.hpp"
#include "glirps/glirps_handles.hpp"

#include "SoundProgramTestSupport.hpp"
#include "SynthApiTestSupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>

// Headless integration tests for the synth lifecycle free functions in
// `glirps/glirps.hpp` (create, configure, start, completion, release).
namespace
{
	// A one-voice program with an instant attack and fast release.
	// `release_tick` makes it a one-shot: the chip-wide gate releases itself
	// at that base tick.
	glirps::SoundProgram makeOneShotProgram(glirps::test_support::TestProgramStorage& storage,
		std::uint32_t release_tick)
	{
		storage.program.base_pitch = glirps::Pitch{.semitone = 36, .cents = 0};
		storage.program.gate.has_release_subtick = true;
		storage.program.gate.release_subtick = release_tick * glirps::subticks_per_base_tick;
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

using glirps::test_support::makeSustainedProgram;
using glirps::test_support::renderOneBaseTick;
using glirps::test_support::test_oversampling_factor;
using glirps::test_support::test_sample_rate_hz;

// A one-shot instance runs create -> configure -> start -> the per-tick
// evaluate/apply/render loop, produces non-silent output, and reports
// `synthIsComplete` once its release tail finishes; then release and destroy.
TEST(SynthLifecycleTests, OneShotLifecycle_RunToCompletion_ProducesAudioThenCompletes)
{
	// ARRANGE
	glirps::test_support::TestProgramStorage storage;
	const glirps::SoundProgram program = makeOneShotProgram(storage, 3);
	const glirps::CreateResult created = glirps::synthCreate(8, 32);
	ASSERT_TRUE(created);
	const glirps::SynthOwner synth_owner(created.synth);
	const glirps::SynthHandle synth = synth_owner.get();
	glirps::synthConfigure(synth, test_sample_rate_hz, test_oversampling_factor);

	// ACT
	const glirps::StartResult started = glirps::synthStart(synth, program, 0);
	ASSERT_TRUE(started);

	float peak = 0.0f;
	bool complete = false;
	constexpr int max_ticks = 400;
	int ticks_run = 0;
	for (; ticks_run < max_ticks; ++ticks_run)
	{
		peak = std::max(peak, renderOneBaseTick(synth, started.instance));
		if (glirps::synthIsComplete(synth, started.instance))
		{
			complete = true;
			break;
		}
	}

	// ASSERT: audible while playing, and eventually complete.
	EXPECT_GT(peak, 0.0f);
	EXPECT_TRUE(complete);
	EXPECT_LT(ticks_run, max_ticks);

	glirps::synthRelease(synth, started.instance);
}

// `synthStart` returns a falsy `StartResult` carrying the expected `StartError`
// once every chip-instance slot is taken.
TEST(SynthLifecycleTests, Start_WhenChipInstancesExhausted_ReturnsFalsyWithError)
{
	// ARRANGE: a single chip-instance slot.
	glirps::test_support::TestProgramStorage storage;
	const glirps::SoundProgram program = makeSustainedProgram(storage);
	const glirps::CreateResult created = glirps::synthCreate(1, 4);
	ASSERT_TRUE(created);
	glirps::SynthHandle synth = created.synth;
	glirps::synthConfigure(synth, test_sample_rate_hz, test_oversampling_factor);

	// ACT
	const glirps::StartResult first = glirps::synthStart(synth, program, 0);
	const glirps::StartResult second = glirps::synthStart(synth, program, 0);

	// ASSERT
	EXPECT_TRUE(first);
	EXPECT_FALSE(second);
	EXPECT_TRUE(second.instance.isNull());
	EXPECT_EQ(second.error, glirps::StartError::chip_instances_exhausted);

	glirps::synthDestroy(synth);
}

// `synthCreate` returns a falsy `CreateResult` carrying `invalid_arguments`
// for a zero pool size, and a truthy result with a valid handle for nonzero
// sizes.
TEST(SynthLifecycleTests, Create_ZeroPoolSize_ReturnsFalsyWithInvalidArguments)
{
	// ACT
	const glirps::CreateResult zero_instances = glirps::synthCreate(0, 4);
	const glirps::CreateResult zero_voices = glirps::synthCreate(4, 0);
	const glirps::CreateResult valid = glirps::synthCreate(4, 4);

	// ASSERT
	EXPECT_FALSE(zero_instances);
	EXPECT_EQ(zero_instances.error, glirps::CreateError::invalid_arguments);
	EXPECT_FALSE(zero_voices);
	EXPECT_EQ(zero_voices.error, glirps::CreateError::invalid_arguments);
	EXPECT_TRUE(valid);

	glirps::synthDestroy(valid.synth);
	glirps::synthDestroy(zero_instances.synth);
	glirps::synthDestroy(zero_voices.synth);
}
