#include "glirps/glirps.hpp"

#include "SoundProgramTestSupport.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace
{
	constexpr std::uint32_t test_sample_rate_hz_synth_concurrency = 48000;
	constexpr std::uint32_t test_oversampling_factor_synth_concurrency = 16;
	constexpr int start_release_iteration_count = 20000;

	glirps::SoundProgram makeMinimalProgramSynthConcurrency(glirps::test_support::TestProgramStorage& storage)
	{
		storage.program.base_pitch = glirps::Pitch{.semitone = 36, .cents = 0};
		glirps::VoiceProgram voice;
		voice.waveform = glirps::Waveform::triangle;
		voice.gain = 1.0f;
		voice.envelope = glirps::EnvelopeSpec{.attack_seconds = 0.0001f,
			.decay_seconds = 0.0001f,
			.sustain_level = 1.0f,
			.release_seconds = 0.0001f};
		glirps::test_support::setVoices(storage, {voice});
		return storage.program;
	}
}

// Checks for data races between `synthStart`/`synthRelease` and `synthRender`
// on two threads. Only the ThreadSanitizer build detects them.
TEST(SynthConcurrencyTests, StartReleaseOnControlThread_RenderOnRenderThread_NoRace)
{
	// ARRANGE
	glirps::test_support::TestProgramStorage storage;
	const glirps::SoundProgram program = makeMinimalProgramSynthConcurrency(storage);
	const glirps::CreateResult created = glirps::synthCreate(4, 16);
	ASSERT_TRUE(created);
	glirps::SynthHandle synth = created.synth;
	glirps::synthConfigure(synth, test_sample_rate_hz_synth_concurrency, test_oversampling_factor_synth_concurrency);
	std::atomic<bool> stop{false};

	// ACT: the render thread repeatedly scans and renders every live slot,
	// exactly as the audio callback thread would; the main thread repeatedly
	// starts and immediately releases an instance without waiting for
	// completion, maximizing how often slot activation and generation
	// change while the render thread may be observing them.
	std::thread render_thread(
		[synth, &stop]
		{
			std::vector<float> output(64, 0.0f);
			while (!stop.load())
				glirps::synthRender(synth, glirps::shims::toSpan(output));
		});

	for (int i = 0; i < start_release_iteration_count; ++i)
	{
		const glirps::StartResult result = glirps::synthStart(synth, program, static_cast<std::uint64_t>(i));
		if (result)
			glirps::synthRelease(synth, result.instance);
	}
	stop.store(true);
	render_thread.join();

	// ASSERT: ThreadSanitizer fails the test if it detects a race.
	SUCCEED();

	glirps::synthDestroy(synth);
}
