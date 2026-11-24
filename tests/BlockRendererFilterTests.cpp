#include "glirps/glirps_render.hpp"

#include "BlockRendererApiTestSupport.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

// Headless integration tests for the shared filter in
// `glirps/glirps_render.hpp`. GLIRPS is CPU-only synthesis producing a sample buffer,
// so no GPU or audio device is used.
using glirps::test_support::peakOf;
using glirps::test_support::test_oversampling_factor;
using glirps::test_support::test_sample_rate_hz;
using glirps::test_support::test_seed;

// setFilter + setFilterCutoff sweep the shared filter over a render and still
// produce non-silent audio.
TEST(BlockRendererFilterTests, Filter_CutoffSweep_ProducesNonSilentAudio)
{
	// ARRANGE
	const glirps::BlockRendererCreateResult created
		= glirps::blockRendererCreate(test_sample_rate_hz, test_oversampling_factor);
	ASSERT_TRUE(created);
	glirps::BlockRendererHandle renderer = created.renderer;
	constexpr std::uint32_t frame_count = test_sample_rate_hz / 4;
	glirps::blockRendererConfigureVoice(renderer, 0, glirps::Waveform::sawtooth, 110.0f, 0.5f, 1.0f, test_seed);
	glirps::blockRendererSetFilter(renderer, 8000.0f, 4.0f, glirps::FilterMode::lowpass, 0.0f);
	glirps::blockRendererSetGate(renderer, 0, true);

	// ACT: sweep cutoff exponentially from 8000 Hz down to 200 Hz.
	std::vector<float> samples(frame_count);
	for (std::uint32_t frame = 0; frame < frame_count; ++frame)
	{
		const float t = static_cast<float>(frame) / static_cast<float>(frame_count - 1);
		glirps::blockRendererSetFilterCutoff(renderer, 8000.0f * std::pow(200.0f / 8000.0f, t));
		samples.at(frame) = glirps::blockRendererRenderOne(renderer);
	}

	// ASSERT
	EXPECT_GT(peakOf(samples), 0.0f);

	glirps::blockRendererDestroy(renderer);
}

// setFilterSaturation changes the filter's feedback-path waveshaping; with a
// filter mode enabled, two different saturation amounts render differently
// for an otherwise identical setup.
TEST(BlockRendererFilterTests, FilterSaturation_DifferentAmounts_ProduceDifferentOutput)
{
	// ARRANGE
	const glirps::BlockRendererCreateResult created
		= glirps::blockRendererCreate(test_sample_rate_hz, test_oversampling_factor);
	ASSERT_TRUE(created);
	glirps::BlockRendererHandle renderer = created.renderer;
	glirps::blockRendererConfigureVoice(renderer, 0, glirps::Waveform::sawtooth, 110.0f, 0.5f, 1.0f, test_seed);
	glirps::blockRendererSetFilter(renderer, 400.0f, 8.0f, glirps::FilterMode::lowpass, 0.0f);
	glirps::blockRendererSetGate(renderer, 0, true);

	// ACT: render at zero saturation, then reset and render the identical
	// setup again with saturation raised.
	std::vector<float> unsaturated_samples(glirps::sub_block_frame_count * 10);
	glirps::blockRendererRender(renderer, glirps::shims::toSpan(unsaturated_samples));

	glirps::blockRendererReconfigure(renderer, test_sample_rate_hz, test_oversampling_factor);
	glirps::blockRendererResetState(renderer);
	glirps::blockRendererConfigureVoice(renderer, 0, glirps::Waveform::sawtooth, 110.0f, 0.5f, 1.0f, test_seed);
	glirps::blockRendererSetFilter(renderer, 400.0f, 8.0f, glirps::FilterMode::lowpass, 0.0f);
	glirps::blockRendererSetFilterSaturation(renderer, 5.0f);
	glirps::blockRendererSetGate(renderer, 0, true);
	std::vector<float> saturated_samples(glirps::sub_block_frame_count * 10);
	glirps::blockRendererRender(renderer, glirps::shims::toSpan(saturated_samples));

	// ASSERT
	EXPECT_NE(unsaturated_samples, saturated_samples);

	glirps::blockRendererDestroy(renderer);
}
