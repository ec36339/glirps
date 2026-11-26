#include "glirps/glirps_render.hpp"

#include "BlockRendererApiTestSupport.hpp"

#include <gtest/gtest.h>

#include <vector>

// Headless integration tests for voice envelopes and gating in
// `glirps/glirps_render.hpp`.
using glirps::test_support::peakOf;
using glirps::test_support::test_frequency_hz;
using glirps::test_support::test_oversampling_factor;
using glirps::test_support::test_sample_rate_hz;
using glirps::test_support::test_seed;

// setVoiceEnvelope + setGate produce audio while gated on and reach
// isComplete after the gate is released and the tail finishes; resetState
// clears that completed state back to an inactive renderer.
TEST(BlockRendererEnvelopeTests, Envelope_GateOnThenOff_ProducesAudioThenCompletes)
{
	// ARRANGE
	const glirps::BlockRendererCreateResult created
		= glirps::blockRendererCreate(test_sample_rate_hz, test_oversampling_factor);
	ASSERT_TRUE(created);
	glirps::BlockRendererHandle renderer = created.renderer;
	glirps::blockRendererConfigureVoice(renderer,
		0,
		glirps::Waveform::sawtooth,
		test_frequency_hz,
		0.5f,
		1.0f,
		test_seed);
	// Rate index 0: the fastest preset, so the render-to-completion loop below
	// finishes quickly.
	glirps::blockRendererSetVoiceEnvelope(renderer, 0, 0, 0, 0, 0.5f);
	glirps::blockRendererSetGate(renderer, 0, true);

	// ACT: render while gated, then release and run to completion.
	std::vector<float> gated_samples(glirps::sub_block_frame_count * 20);
	glirps::blockRendererRender(renderer, glirps::shims::toSpan(gated_samples));
	glirps::blockRendererSetGate(renderer, 0, false);

	bool complete = false;
	constexpr int max_blocks = 400;
	std::vector<float> tail_block(glirps::sub_block_frame_count);
	int blocks_run = 0;
	for (; blocks_run < max_blocks; ++blocks_run)
	{
		glirps::blockRendererRender(renderer, glirps::shims::toSpan(tail_block));
		if (glirps::blockRendererIsComplete(renderer))
		{
			complete = true;
			break;
		}
	}

	// ASSERT
	EXPECT_GT(peakOf(gated_samples), 0.0f);
	EXPECT_TRUE(complete);
	EXPECT_LT(blocks_run, max_blocks);

	// ACT: resetState returns the renderer to a fresh, reusable state; a
	// voice configured and gated on it afterward is no longer complete.
	glirps::blockRendererResetState(renderer);
	glirps::blockRendererConfigureVoice(renderer,
		0,
		glirps::Waveform::sawtooth,
		test_frequency_hz,
		0.5f,
		1.0f,
		test_seed);
	glirps::blockRendererSetVoiceEnvelope(renderer, 0, 0, 0, 0, 0.5f);
	glirps::blockRendererSetGate(renderer, 0, true);

	// ASSERT
	EXPECT_FALSE(glirps::blockRendererIsComplete(renderer));

	glirps::blockRendererDestroy(renderer);
}
