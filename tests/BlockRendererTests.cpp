#include "impl/BlockRenderer.hpp"
#include "impl/ChipInstance.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

TEST(BlockRendererTests, Render_RingModSourceSet_MatchesDirectChipInstanceRingMod)
{
	// An inactive voice's noise seed never reaches the output (only active
	// voices are summed), so the direct `ChipInstance`'s own construction
	// seed need not match `BlockRenderer`'s internal default -- only the two
	// configured voices' own `setVoice`/`configureVoice` seeds need to agree,
	// which they do below (ring-mod/hard-sync routing).
	// ARRANGE
	constexpr std::uint32_t sample_rate_hz = 48000;
	constexpr std::uint32_t frame_count = 128;
	glirps::impl::BlockRenderer renderer(sample_rate_hz);
	renderer.configureVoice(0, glirps::Waveform::triangle, 220.0f, 0.5f, 1.0f, 0x1111u);
	renderer.configureVoice(1, glirps::Waveform::triangle, 55.0f, 0.5f, 1.0f, 0x2222u);
	renderer.setRingModSource(0, 1);

	glirps::impl::ChipInstance direct_chip(sample_rate_hz, glirps::max_voices_per_instance, 0);
	direct_chip.setVoice(0, glirps::Waveform::triangle, 220.0f, 0.5f, 1.0f, 0x1111u);
	direct_chip.setVoice(1, glirps::Waveform::triangle, 55.0f, 0.5f, 1.0f, 0x2222u);
	direct_chip.setRingModSource(0, 1);

	// ACT
	std::vector<float> via_renderer(frame_count);
	renderer.render(via_renderer);
	std::vector<float> via_direct_chip(frame_count);
	for (float& sample : via_direct_chip)
		sample = direct_chip.advance();

	// ASSERT
	EXPECT_EQ(via_renderer, via_direct_chip);
}

TEST(BlockRendererTests, Render_HardSyncSourceSet_MatchesDirectChipInstanceHardSync)
{
	// See the ring-mod test above for why the direct `ChipInstance`'s own
	// construction seed need not match `BlockRenderer`'s internal default.
	// ARRANGE
	constexpr std::uint32_t sample_rate_hz = 48000;
	constexpr std::uint32_t frame_count = 128;
	glirps::impl::BlockRenderer renderer(sample_rate_hz);
	renderer.configureVoice(0, glirps::Waveform::triangle, 440.0f, 0.5f, 1.0f, 0x3333u);
	renderer.configureVoice(1, glirps::Waveform::triangle, 55.0f, 0.5f, 1.0f, 0x4444u);
	renderer.setHardSyncSource(0, 1);

	glirps::impl::ChipInstance direct_chip(sample_rate_hz, glirps::max_voices_per_instance, 0);
	direct_chip.setVoice(0, glirps::Waveform::triangle, 440.0f, 0.5f, 1.0f, 0x3333u);
	direct_chip.setVoice(1, glirps::Waveform::triangle, 55.0f, 0.5f, 1.0f, 0x4444u);
	direct_chip.setHardSyncSource(0, 1);

	// ACT
	std::vector<float> via_renderer(frame_count);
	renderer.render(via_renderer);
	std::vector<float> via_direct_chip(frame_count);
	for (float& sample : via_direct_chip)
		sample = direct_chip.advance();

	// ASSERT
	EXPECT_EQ(via_renderer, via_direct_chip);
}

TEST(BlockRendererTests, Render_SameConfigurationSameSeed_ByteIdentical)
{
	// ARRANGE
	constexpr std::uint32_t sample_rate_hz = 48000;
	constexpr std::uint32_t frame_count = 128;
	glirps::impl::BlockRenderer first(sample_rate_hz);
	glirps::impl::BlockRenderer second(sample_rate_hz);
	first.configureVoice(0, glirps::Waveform::triangle, 220.0f, 0.5f, 1.0f, 0x5a5au);
	second.configureVoice(0, glirps::Waveform::triangle, 220.0f, 0.5f, 1.0f, 0x5a5au);

	// ACT
	std::vector<float> out_first(frame_count);
	std::vector<float> out_second(frame_count);
	first.render(out_first);
	second.render(out_second);

	// ASSERT
	EXPECT_EQ(out_first, out_second);
}
