#include "glirps/glirps_render.hpp"

#include "BlockRendererApiTestSupport.hpp"

#include <gtest/gtest.h>

#include <vector>

// Headless integration tests for ring modulation and hard sync in
// `glirps/glirps_render.hpp`.
using glirps::test_support::peakOf;
using glirps::test_support::test_frequency_hz;
using glirps::test_support::test_oversampling_factor;
using glirps::test_support::test_sample_rate_hz;
using glirps::test_support::test_seed;

// setRingModSource multiplies voice 0's carrier by voice 1's silent modulator
// and still produces non-silent audio.
TEST(BlockRendererModulationTests, RingMod_TwoVoices_ProducesNonSilentAudio)
{
	// ARRANGE
	const glirps::BlockRendererCreateResult created
		= glirps::blockRendererCreate(test_sample_rate_hz, test_oversampling_factor);
	ASSERT_TRUE(created);
	glirps::BlockRendererHandle renderer = created.renderer;
	glirps::blockRendererConfigureVoice(renderer,
		0,
		glirps::Waveform::triangle,
		test_frequency_hz,
		0.5f,
		1.0f,
		test_seed);
	glirps::blockRendererConfigureVoice(renderer, 1, glirps::Waveform::triangle, 331.0f, 0.5f, 0.0f, test_seed);
	glirps::blockRendererSetRingModSource(renderer, 0, 1);
	glirps::blockRendererSetGate(renderer, 0, true);
	glirps::blockRendererSetGate(renderer, 1, true);

	// ACT
	std::vector<float> samples(glirps::sub_block_frame_count * 20);
	glirps::blockRendererRender(renderer, glirps::shims::toSpan(samples));

	// ASSERT
	EXPECT_GT(peakOf(samples), 0.0f);

	glirps::blockRendererDestroy(renderer);
}

// setHardSyncSource phase-resets voice 1 from voice 0's overflow and still
// produces non-silent audio.
TEST(BlockRendererModulationTests, HardSync_TwoVoices_ProducesNonSilentAudio)
{
	// ARRANGE
	const glirps::BlockRendererCreateResult created
		= glirps::blockRendererCreate(test_sample_rate_hz, test_oversampling_factor);
	ASSERT_TRUE(created);
	glirps::BlockRendererHandle renderer = created.renderer;
	glirps::blockRendererConfigureVoice(renderer, 0, glirps::Waveform::sawtooth, 110.0f, 0.5f, 0.0f, test_seed);
	glirps::blockRendererConfigureVoice(renderer, 1, glirps::Waveform::sawtooth, 110.0f, 0.5f, 1.0f, test_seed);
	glirps::blockRendererSetHardSyncSource(renderer, 1, 0);
	glirps::blockRendererSetGate(renderer, 0, true);
	glirps::blockRendererSetGate(renderer, 1, true);

	// ACT
	std::vector<float> samples(glirps::sub_block_frame_count * 20);
	glirps::blockRendererRender(renderer, glirps::shims::toSpan(samples));

	// ASSERT
	EXPECT_GT(peakOf(samples), 0.0f);

	glirps::blockRendererDestroy(renderer);
}
