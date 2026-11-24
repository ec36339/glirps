#include "glirps/glirps_render.hpp"

#include "BlockRendererApiTestSupport.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

// Headless integration tests for output gain and the decimator in
// `glirps/glirps_render.hpp`. GLIRPS is CPU-only synthesis producing a sample buffer,
// so no GPU or audio device is used.
using glirps::test_support::peakOf;
using glirps::test_support::test_frequency_hz;
using glirps::test_support::test_oversampling_factor;
using glirps::test_support::test_sample_rate_hz;
using glirps::test_support::test_seed;

// setOutputGain applies with no ramp: halving it exactly halves the peak of
// an otherwise identical render on an independently created renderer.
TEST(BlockRendererOutputTests, OutputGain_Halved_HalvesPeak)
{
	// ARRANGE
	const glirps::BlockRendererCreateResult full_gain_created
		= glirps::blockRendererCreate(test_sample_rate_hz, test_oversampling_factor);
	ASSERT_TRUE(full_gain_created);
	glirps::BlockRendererHandle full_gain_renderer = full_gain_created.renderer;
	glirps::blockRendererConfigureVoice(full_gain_renderer,
		0,
		glirps::Waveform::triangle,
		test_frequency_hz,
		0.5f,
		1.0f,
		test_seed);
	glirps::blockRendererSetGate(full_gain_renderer, 0, true);

	const glirps::BlockRendererCreateResult half_gain_created
		= glirps::blockRendererCreate(test_sample_rate_hz, test_oversampling_factor);
	ASSERT_TRUE(half_gain_created);
	glirps::BlockRendererHandle half_gain_renderer = half_gain_created.renderer;
	glirps::blockRendererConfigureVoice(half_gain_renderer,
		0,
		glirps::Waveform::triangle,
		test_frequency_hz,
		0.5f,
		1.0f,
		test_seed);
	glirps::blockRendererSetOutputGain(half_gain_renderer, 0.5f);
	glirps::blockRendererSetGate(half_gain_renderer, 0, true);

	// ACT
	std::vector<float> full_gain_samples(glirps::sub_block_frame_count * 10);
	glirps::blockRendererRender(full_gain_renderer, glirps::shims::toSpan(full_gain_samples));
	std::vector<float> half_gain_samples(glirps::sub_block_frame_count * 10);
	glirps::blockRendererRender(half_gain_renderer, glirps::shims::toSpan(half_gain_samples));

	// ASSERT
	const float full_gain_peak = peakOf(full_gain_samples);
	EXPECT_GT(full_gain_peak, 0.0f);
	EXPECT_NEAR(peakOf(half_gain_samples), full_gain_peak * 0.5f, full_gain_peak * 0.01f);

	glirps::blockRendererDestroy(full_gain_renderer);
	glirps::blockRendererDestroy(half_gain_renderer);
}

// getDecimatorGroupDelayInFrames reports a positive delay, and reconfigure
// to a different sample rate at the same oversampling factor leaves it
// unchanged: the decimator's tap count is a fixed fraction of the oversampled
// rate, so it is invariant under a rate change at a fixed factor.
TEST(BlockRendererOutputTests, GetDecimatorGroupDelayInFrames_ReconfigureSameFactorDifferentRate_Unchanged)
{
	// ARRANGE
	const glirps::BlockRendererCreateResult created
		= glirps::blockRendererCreate(test_sample_rate_hz, test_oversampling_factor);
	ASSERT_TRUE(created);
	glirps::BlockRendererHandle renderer = created.renderer;
	const std::uint32_t delay_before = glirps::blockRendererGetDecimatorGroupDelayInFrames(renderer);

	// ACT
	glirps::blockRendererReconfigure(renderer, 44100, test_oversampling_factor);
	const std::uint32_t delay_after = glirps::blockRendererGetDecimatorGroupDelayInFrames(renderer);

	// ASSERT
	EXPECT_GT(delay_before, 0u);
	EXPECT_EQ(delay_before, delay_after);

	glirps::blockRendererDestroy(renderer);
}
