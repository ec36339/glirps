#include "glirps/glirps.hpp"
#include "glirps/glirps_handles.hpp"

#include "ScopedAllocationCounter.hpp"
#include "SoundProgramTestSupport.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace
{
	constexpr std::uint32_t test_sample_rate_hz_synth_allocation = 48000;
	constexpr std::uint32_t test_oversampling_factor_synth_allocation = 16;

	// `storage` SHALL outlive every use of the returned `SoundProgram`.
	glirps::SoundProgram makeMinimalProgramSynthAllocation(glirps::test_support::TestProgramStorage& storage)
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

TEST(SynthAllocationTests, Render_OneCall_AllocatesNothing)
{
	// ARRANGE: `synthCreate` allocates the pools, so it stays outside the
	// counter scope.
	glirps::test_support::TestProgramStorage storage;
	const glirps::SoundProgram program = makeMinimalProgramSynthAllocation(storage);
	const glirps::CreateResult created = glirps::synthCreate(8, 32);
	ASSERT_TRUE(created);
	const glirps::SynthOwner synth_owner(created.synth);
	const glirps::SynthHandle synth = synth_owner.get();
	glirps::synthConfigure(synth, test_sample_rate_hz_synth_allocation, test_oversampling_factor_synth_allocation);
	ASSERT_TRUE(glirps::synthStart(synth, program, 0));
	std::vector<float> output(256, 0.0f);

	// ACT
	{
		const glirps::test_support::ScopedAllocationCounter counter;
		glirps::synthRender(synth, glirps::shims::toSpan(output));

		// ASSERT: the render path allocates nothing, since it runs on a real-time
		// thread.
		EXPECT_EQ(counter.getAllocationCount(), 0u);
	}
}

TEST(SynthAllocationTests, Render_SeveralConsecutiveCalls_AllocatesNothing)
{
	// ARRANGE: a single-call check would not catch a rate-derived constant
	// wrongly recomputed lazily on the first `render` call rather than in
	// `configure`, where every rate-dependent constant must be derived: the
	// render path must not allocate.
	glirps::test_support::TestProgramStorage storage;
	const glirps::SoundProgram program = makeMinimalProgramSynthAllocation(storage);
	const glirps::CreateResult created = glirps::synthCreate(8, 32);
	ASSERT_TRUE(created);
	const glirps::SynthOwner synth_owner(created.synth);
	const glirps::SynthHandle synth = synth_owner.get();
	glirps::synthConfigure(synth, test_sample_rate_hz_synth_allocation, test_oversampling_factor_synth_allocation);
	ASSERT_TRUE(glirps::synthStart(synth, program, 0));
	std::vector<float> output(256, 0.0f);

	// ACT
	{
		const glirps::test_support::ScopedAllocationCounter counter;
		for (int call = 0; call < 5; ++call)
		{
			glirps::synthRender(synth, glirps::shims::toSpan(output));
		}

		// ASSERT
		EXPECT_EQ(counter.getAllocationCount(), 0u);
	}
}

TEST(SynthAllocationTests, Start_OneCall_AllocatesNothing)
{
	// ARRANGE: pools already allocated in `synthCreate`. `program` outlives the
	// instance because the synth references it without copying.
	glirps::test_support::TestProgramStorage storage;
	const glirps::SoundProgram program = makeMinimalProgramSynthAllocation(storage);
	const glirps::CreateResult created = glirps::synthCreate(8, 32);
	ASSERT_TRUE(created);
	const glirps::SynthOwner synth_owner(created.synth);
	const glirps::SynthHandle synth = synth_owner.get();
	glirps::synthConfigure(synth, test_sample_rate_hz_synth_allocation, test_oversampling_factor_synth_allocation);

	// ACT
	glirps::StartResult result{};
	{
		const glirps::test_support::ScopedAllocationCounter counter;
		result = glirps::synthStart(synth, program, 0);

		// ASSERT: starting an instance allocates nothing, since `start` stores a
		// pointer to the program rather than copying it.
		EXPECT_EQ(counter.getAllocationCount(), 0u);
	}

	ASSERT_TRUE(result);
}
