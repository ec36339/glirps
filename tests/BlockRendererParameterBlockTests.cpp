#include "glirps/glirps_render.hpp"

#include "BlockRendererApiTestSupport.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

// Headless integration tests for applying a `ParameterBlock` in
// `glirps/glirps_render.hpp`.
using glirps::test_support::peakOf;
using glirps::test_support::test_frequency_hz;
using glirps::test_support::test_oversampling_factor;
using glirps::test_support::test_sample_rate_hz;

namespace
{
	// A one-voice, wide-open-filter `ParameterBlock` targeting voice 0, so the
	// `applyBlock`/`applyBaseBlock` round trip can be exercised with no
	// `SoundProgram`/`Synth` involved.
	glirps::ParameterBlock makeOneVoiceBlock(glirps::Waveform waveform, float frequency_hz, bool gate_on) noexcept
	{
		glirps::ParameterBlock block;
		block.voice_count = 1;
		block.voices.front() = glirps::VoiceParameters{.waveform = waveform,
			.frequency_hz = frequency_hz,
			.pulse_width = 0.5f,
			.gain = 1.0f,
			.gate_on = gate_on};
		return block;
	}
}

// applyBaseBlock establishes voice 0's base state with no ramp; applyBlock
// then releases the gate, and the renderer runs to isComplete.
TEST(BlockRendererParameterBlockTests, ApplyBaseBlockThenApplyBlock_GateRelease_RunsToCompletion)
{
	// ARRANGE
	const glirps::BlockRendererCreateResult created
		= glirps::blockRendererCreate(test_sample_rate_hz, test_oversampling_factor);
	ASSERT_TRUE(created);
	glirps::BlockRendererHandle renderer = created.renderer;
	glirps::blockRendererSetVoiceEnvelope(renderer, 0, 1, 2, 3, 0.5f);
	const glirps::ParameterBlock base_block = makeOneVoiceBlock(glirps::Waveform::triangle, test_frequency_hz, true);
	glirps::blockRendererApplyBaseBlock(renderer, base_block);

	// ACT: render while gated, then apply a block with the gate released and
	// run to completion.
	std::vector<float> gated_samples(glirps::sub_block_frame_count * 5);
	glirps::blockRendererRender(renderer, glirps::shims::toSpan(gated_samples));

	const glirps::ParameterBlock release_block
		= makeOneVoiceBlock(glirps::Waveform::triangle, test_frequency_hz, false);
	glirps::blockRendererApplyBlock(renderer, release_block);

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

	glirps::blockRendererDestroy(renderer);
}
