#include "glirps/glirps_render.hpp"

#include "BlockRendererApiTestSupport.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

// Headless integration tests for voice configuration and rendering in
// `glirps/glirps_render.hpp`. GLIRPS is CPU-only synthesis producing a sample buffer,
// so no GPU or audio device is used.
using glirps::test_support::peakOf;
using glirps::test_support::test_frequency_hz;
using glirps::test_support::test_oversampling_factor;
using glirps::test_support::test_sample_rate_hz;
using glirps::test_support::test_seed;

// create -> configureVoice -> render produces non-silent audio, then destroy.
TEST(BlockRendererVoiceTests, ConfigureVoice_Render_ProducesNonSilentAudio)
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

	// ACT
	std::vector<float> samples(glirps::sub_block_frame_count * 20);
	glirps::blockRendererRender(renderer, glirps::shims::toSpan(samples));

	// ASSERT
	EXPECT_GT(peakOf(samples), 0.0f);

	glirps::blockRendererDestroy(renderer);
}

// setVoiceFrequency + renderOne (per-sample rendering with a per-frame
// parameter sweep) produces non-silent audio.
TEST(BlockRendererVoiceTests, RenderOne_FrequencySweep_ProducesNonSilentAudio)
{
	// ARRANGE
	const glirps::BlockRendererCreateResult created
		= glirps::blockRendererCreate(test_sample_rate_hz, test_oversampling_factor);
	ASSERT_TRUE(created);
	glirps::BlockRendererHandle renderer = created.renderer;
	constexpr std::uint32_t frame_count = test_sample_rate_hz / 4;
	glirps::blockRendererConfigureVoice(renderer, 0, glirps::Waveform::sawtooth, 110.0f, 0.5f, 1.0f, test_seed);
	glirps::blockRendererSetGate(renderer, 0, true);

	// ACT: sweep frequency linearly from 110 Hz to 880 Hz across the render.
	std::vector<float> samples(frame_count);
	for (std::uint32_t frame = 0; frame < frame_count; ++frame)
	{
		const float t = static_cast<float>(frame) / static_cast<float>(frame_count - 1);
		glirps::blockRendererSetVoiceFrequency(renderer, 0, 110.0f + (880.0f - 110.0f) * t);
		samples.at(frame) = glirps::blockRendererRenderOne(renderer);
	}

	// ASSERT
	EXPECT_GT(peakOf(samples), 0.0f);

	glirps::blockRendererDestroy(renderer);
}

// seedVoiceNoise reseeds one voice's LFSR independently of the others: two
// noise voices seeded differently on the same renderer produce different
// output.
TEST(BlockRendererVoiceTests, SeedVoiceNoise_DifferentSeeds_ProduceDifferentOutput)
{
	// ARRANGE
	const glirps::BlockRendererCreateResult created
		= glirps::blockRendererCreate(test_sample_rate_hz, test_oversampling_factor);
	ASSERT_TRUE(created);
	glirps::BlockRendererHandle renderer = created.renderer;
	glirps::blockRendererConfigureVoice(renderer, 0, glirps::Waveform::noise, test_frequency_hz, 0.5f, 1.0f, test_seed);
	glirps::blockRendererSetGate(renderer, 0, true);
	std::vector<float> first_seed_samples(glirps::sub_block_frame_count * 4);
	glirps::blockRendererRender(renderer, glirps::shims::toSpan(first_seed_samples));

	// ACT: reconfigure back to the construction state, then render the same
	// voice again with a different noise seed.
	glirps::blockRendererReconfigure(renderer, test_sample_rate_hz, test_oversampling_factor);
	glirps::blockRendererResetState(renderer);
	glirps::blockRendererConfigureVoice(renderer, 0, glirps::Waveform::noise, test_frequency_hz, 0.5f, 1.0f, test_seed);
	glirps::blockRendererSeedVoiceNoise(renderer, 0, test_seed + 1);
	glirps::blockRendererSetGate(renderer, 0, true);
	std::vector<float> second_seed_samples(glirps::sub_block_frame_count * 4);
	glirps::blockRendererRender(renderer, glirps::shims::toSpan(second_seed_samples));

	// ASSERT
	EXPECT_NE(first_seed_samples, second_seed_samples);

	glirps::blockRendererDestroy(renderer);
}
