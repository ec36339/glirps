#include "impl/Synth.hpp"

#include "glirps/constants.hpp"
#include "glirps/frequency.hpp"

#include "SoundProgramTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace
{
	constexpr std::uint32_t test_sample_rate_hz_external_control_one_shot = 48000;

	// A one-voice, one-shot triangle program with full sustain.
	glirps::SoundProgram oneShotTriangleProgram(glirps::test_support::TestProgramStorage& storage)
	{
		storage.program.base_pitch = glirps::Pitch{.semitone = 36, .cents = 0};
		storage.program.gate.has_release_subtick = true;
		storage.program.gate.release_subtick = 4U * glirps::subticks_per_base_tick;
		storage.program.filter.cutoff_hz = 800.0f;
		storage.program.output_gain = 0.8f;
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

// `updateControlParams` applies to every live instance, one-shot included: a
// release tick governs when the gate releases and says nothing about whether
// the instance's pitch, cutoff, or gain may be driven from outside. A consumer
// driving pitch per tick needs this reach on one-shot instances too.
TEST(SynthExternalControlOneShotTests, UpdateControlParams_OneShotInstance_TakesEffect)
{
	// ARRANGE
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_external_control_one_shot);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage storage;
	auto program = oneShotTriangleProgram(storage);
	auto handle = synth.start(program, 0);
	ASSERT_TRUE(handle.has_value());
	std::array<glirps::ParameterBlock, 1> blocks;

	// ACT: the first evaluated tick, before any external control is applied.
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);

	// ASSERT: the baseline is exactly the authored pitch, cutoff, and gain.
	const float baseline_frequency = glirps::frequencyHertz(36, 0.0f);
	EXPECT_NEAR(blocks.at(0).voices.at(0).frequency_hz, baseline_frequency, 0.01f);
	EXPECT_FLOAT_EQ(blocks.at(0).cutoff_hz, 800.0f);
	EXPECT_FLOAT_EQ(blocks.at(0).output_gain, 0.8f);

	// ACT: a flagged pitch offset, cutoff override, and gain multiplier.
	synth.updateControlParams(*handle,
		glirps::ControlParams{
			.flags
			= glirps::ControlFlag::pitch_cents | glirps::ControlFlag::cutoff_hz | glirps::ControlFlag::gain_multiplier,
			.pitch_cents = 1200.0f,
			.cutoff_hz = 2400.0f,
			.gain_multiplier = 0.5f,
		});
	ASSERT_EQ(synth.evaluateTick(*handle, blocks), 1u);

	// ASSERT
	const float shifted_frequency = glirps::frequencyHertz(36, 1200.0f);
	EXPECT_NEAR(blocks.at(0).voices.at(0).frequency_hz, shifted_frequency, 0.01f);
	EXPECT_FLOAT_EQ(blocks.at(0).cutoff_hz, 2400.0f);
	EXPECT_FLOAT_EQ(blocks.at(0).output_gain, 0.4f);
}

// `updateControlParams` on an expired handle is a no-op.
TEST(SynthExternalControlOneShotTests, UpdateControlParams_ExpiredHandle_IsIgnored)
{
	// ARRANGE: release the first instance and reclaim its slot with a second.
	glirps::impl::Synth synth(1, 4);
	synth.configure(test_sample_rate_hz_external_control_one_shot);
	synth.setGritScale(0.0f);
	glirps::test_support::TestProgramStorage storage;
	auto program = oneShotTriangleProgram(storage);
	auto first = synth.start(program, 0);
	ASSERT_TRUE(first.has_value());
	synth.release(*first);

	// A released slot is not reclaimable until `render` itself acknowledges
	// the release, so the render thread never races a slot reuse.
	std::vector<float> acknowledge_release(glirps::sub_block_frame_count, 0.0f);
	synth.render(acknowledge_release);
	auto second = synth.start(program, 0);
	ASSERT_TRUE(second.has_value());

	// ACT: a stale call against the released (expired) handle.
	synth.updateControlParams(*first,
		glirps::ControlParams{
			.flags
			= glirps::ControlFlag::pitch_cents | glirps::ControlFlag::cutoff_hz | glirps::ControlFlag::gain_multiplier,
			.pitch_cents = 1200.0f,
			.cutoff_hz = 2400.0f,
			.gain_multiplier = 0.5f,
		});

	// ASSERT: the new occupant is unaffected, still its own authored state.
	std::array<glirps::ParameterBlock, 1> blocks;
	ASSERT_EQ(synth.evaluateTick(*second, blocks), 1u);
	const float baseline_frequency = glirps::frequencyHertz(36, 0.0f);
	EXPECT_NEAR(blocks.at(0).voices.at(0).frequency_hz, baseline_frequency, 0.01f);
	EXPECT_FLOAT_EQ(blocks.at(0).cutoff_hz, 800.0f);
	EXPECT_FLOAT_EQ(blocks.at(0).output_gain, 0.8f);
}
