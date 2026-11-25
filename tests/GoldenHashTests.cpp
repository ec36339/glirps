#include "glirps/glirps.hpp"
#include "glirps/glirps_handles.hpp"

#include "SoundProgramTestSupport.hpp"
#include "SynthApiTestSupport.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <set>
#include <span>
#include <string>
#include <vector>

// Golden hash tests: each program is rendered through the public API, converted
// to 16-bit PCM, and hashed. A changed hash means changed sound output.
// Updating a stored hash is a deliberate change.
namespace
{
	using glirps::test_support::TestProgramStorage;

	// Floating-point output depends on the standard and math libraries, so the
	// stored hashes hold for the Linux toolchain only.
#ifdef __linux__
	constexpr bool stored_hashes_apply = true;
#else
	constexpr bool stored_hashes_apply = false;
#endif

	constexpr std::uint32_t render_base_ticks = 30;
	constexpr std::uint32_t release_base_tick = 20;
	constexpr std::uint32_t sub_blocks_per_base_tick
		= glirps::test_support::frames_per_base_tick / glirps::sub_block_frame_count;
	constexpr std::uint32_t frames_per_rendered_tick = sub_blocks_per_base_tick * glirps::sub_block_frame_count;

	// Which feature a case adds to the base program.
	enum class Feature
	{
		none,
		ring_modulation,
		hard_sync,
		lowpass,
		bandpass,
		highpass,
		notch,
		tables,
	};

	struct GoldenCase
	{
		const char* name = "";
		glirps::Waveform waveform = glirps::Waveform::pulse;
		Feature feature = Feature::none;
		float grit_scale = 1.0f;
		std::uint64_t seed = 1;
		std::uint32_t onset_delay_base_ticks = 0;
		std::uint64_t expected_hash = 0;
	};

	void PrintTo(const GoldenCase& golden, std::ostream* stream)
	{
		*stream << golden.name;
	}

	constexpr std::array golden_cases = {
		GoldenCase{.name = "triangle", .waveform = glirps::Waveform::triangle,
			.expected_hash = 0x256eb3e210941382ULL},
		GoldenCase{.name = "sawtooth", .waveform = glirps::Waveform::sawtooth,
			.expected_hash = 0xcacf34b173303f31ULL},
		GoldenCase{.name = "pulse",
			.expected_hash = 0xcfe11c92e932715bULL},
		GoldenCase{.name = "noise", .waveform = glirps::Waveform::noise,
			.expected_hash = 0x930ce9d13ae18ddbULL},
		GoldenCase{.name = "ring_modulation", .waveform = glirps::Waveform::triangle,
			.feature = Feature::ring_modulation,
			.expected_hash = 0x006fbcb363e284f2ULL},
		GoldenCase{.name = "hard_sync", .waveform = glirps::Waveform::sawtooth, .feature = Feature::hard_sync,
			.expected_hash = 0x9205e60b1194afa7ULL},
		GoldenCase{.name = "filter_lowpass", .waveform = glirps::Waveform::sawtooth, .feature = Feature::lowpass,
			.expected_hash = 0x0144da1fc5c62edaULL},
		GoldenCase{.name = "filter_bandpass", .waveform = glirps::Waveform::sawtooth, .feature = Feature::bandpass,
			.expected_hash = 0x463d5a067bff5888ULL},
		GoldenCase{.name = "filter_highpass", .waveform = glirps::Waveform::sawtooth, .feature = Feature::highpass,
			.expected_hash = 0xcbbf08ca1efb86cfULL},
		GoldenCase{.name = "filter_notch", .waveform = glirps::Waveform::sawtooth, .feature = Feature::notch,
			.expected_hash = 0x8b7eb77263f0a6a2ULL},
		GoldenCase{.name = "tables", .feature = Feature::tables,
			.expected_hash = 0x378e0432f366f411ULL},
		GoldenCase{.name = "onset_delay", .onset_delay_base_ticks = 5,
			.expected_hash = 0x1a4cb7368962573bULL},
		// Seed 1, the default, draws no pitch step; this one draws a different one.
		GoldenCase{.name = "variation_seed", .seed = 2,
			.expected_hash = 0xc761b08b9ed36c87ULL},
		GoldenCase{.name = "grit_scale_zero", .grit_scale = 0.0f,
			.expected_hash = 0xef119eabf2dff12eULL},
		GoldenCase{.name = "grit_scale_half", .grit_scale = 0.5f,
			.expected_hash = 0xce57a0a02a1e19afULL},
	};

	void addFilter(glirps::SoundProgram& program, glirps::FilterModeSet modes)
	{
		program.filter.modes = modes;
		program.filter.cutoff_hz = 1200.0f;
		program.filter.resonance_q = 4.0f;
	}

	// Builds the program of `golden`. `storage` SHALL outlive every use of the
	// returned program.
	glirps::SoundProgram makeProgram(TestProgramStorage& storage, const GoldenCase& golden)
	{
		glirps::SoundProgram& program = storage.program;
		program.base_pitch = glirps::Pitch{.semitone = 45, .cents = 7};
		program.gate.has_release_subtick = true;
		program.gate.release_subtick = release_base_tick * glirps::subticks_per_base_tick;
		// Seeded pitch variation, so different seeds sound different.
		glirps::test_support::setScale(storage, {0, 2, 3, 5, 7, 8, 10});

		glirps::VoiceProgram voice;
		voice.waveform = golden.waveform;
		voice.pulse_width = 0.3f;
		voice.envelope = glirps::EnvelopeSpec{.attack_seconds = 0.01f,
			.decay_seconds = 0.05f,
			.sustain_level = 0.6f,
			.release_seconds = 0.08f};
		std::vector<glirps::VoiceProgram> voices{voice};

		switch (golden.feature)
		{
		case Feature::none:
			break;
		case Feature::ring_modulation:
		case Feature::hard_sync:
		{
			// A second, silent voice as the modulation source.
			glirps::VoiceProgram source = voice;
			source.gain = 0.0f;
			source.tables.has_arpeggio = true;
			source.tables.arpeggio = glirps::test_support::makeIntTable(storage, {19});
			voices.push_back(source);
			if (golden.feature == Feature::ring_modulation)
			{
				voices.front().has_ring_mod_source = true;
				voices.front().ring_mod_source = 1;
			}
			else
			{
				voices.front().has_hard_sync_source = true;
				voices.front().hard_sync_source = 1;
			}
			break;
		}
		case Feature::lowpass:
			addFilter(program, {.lowpass = true});
			break;
		case Feature::bandpass:
			addFilter(program, {.lowpass = false, .bandpass = true});
			break;
		case Feature::highpass:
			addFilter(program, {.lowpass = false, .highpass = true});
			break;
		case Feature::notch:
			addFilter(program, {.lowpass = false, .notch = true});
			break;
		case Feature::tables:
		{
			glirps::VoiceTables& tables = voices.front().tables;
			tables.has_waveform = true;
			tables.waveform = glirps::test_support::makeWaveformTable(
				storage, {glirps::Waveform::pulse, glirps::Waveform::sawtooth}, 0, 4);
			tables.has_arpeggio = true;
			tables.arpeggio = glirps::test_support::makeIntTable(
				storage, {0, 4, 7}, 0, 2, {std::nullopt, glirps::ValueRange<std::int32_t>{.min = 2, .max = 5}});
			tables.has_pitch_slide = true;
			tables.pitch_slide = glirps::test_support::makeFloatTable(storage, {15.0f});
			tables.has_pulse_width = true;
			tables.pulse_width = glirps::test_support::makeFloatTable(storage, {0.1f, 0.5f, 0.8f}, 0, 3);
			tables.has_gain = true;
			tables.gain = glirps::test_support::makeFloatTable(storage, {1.0f, 0.4f}, 0, 5);
			tables.has_gate = true;
			tables.gate = glirps::test_support::makeBoolTable(storage, {true, true, true, false}, 0, 2);
			addFilter(program, {.lowpass = true});
			program.filter.tables.has_cutoff = true;
			program.filter.tables.cutoff
				= glirps::test_support::makeFloatTable(storage, {500.0f, 2000.0f, 6000.0f}, 0, 4);
			break;
		}
		}

		glirps::test_support::setVoices(storage, std::move(voices));
		return program;
	}

	// Renders `golden` for a fixed duration. Blocks of one base tick are spaced
	// evenly over its sub-blocks.
	std::vector<float> render(const GoldenCase& golden)
	{
		TestProgramStorage storage;
		const glirps::SoundProgram program = makeProgram(storage, golden);

		const glirps::CreateResult created = glirps::synthCreate(1, glirps::max_voices_per_instance);
		if (!created)
			return {};
		const glirps::SynthOwner synth(created.synth);
		glirps::synthConfigure(created.synth,
			glirps::test_support::test_sample_rate_hz,
			glirps::test_support::test_oversampling_factor);
		glirps::synthSetGritScale(created.synth, golden.grit_scale);
		const auto started
			= glirps::synthStart(created.synth, program, golden.seed, golden.onset_delay_base_ticks);
		if (!started)
			return {};

		std::vector<float> samples(std::size_t{render_base_ticks} * frames_per_rendered_tick, 0.0f);
		std::span<float> remaining(samples);
		for (std::uint32_t tick = 0; tick < render_base_ticks; ++tick)
		{
			std::vector<glirps::ParameterBlock> blocks(
				glirps::synthGetBlocksPerBaseTick(created.synth, started.instance));
			const std::uint32_t written
				= glirps::synthEvaluateTick(created.synth, started.instance, glirps::shims::toSpan(blocks));
			std::uint32_t rendered_sub_blocks = 0;
			std::uint32_t block_index = 0;
			for (const glirps::ParameterBlock& block : std::span(blocks).first(written))
			{
				glirps::synthApplyBlock(created.synth, block);
				++block_index;
				const std::uint32_t end = sub_blocks_per_base_tick * block_index / written;
				const std::size_t frames
					= std::size_t{end - rendered_sub_blocks} * glirps::sub_block_frame_count;
				rendered_sub_blocks = end;
				if (frames == 0)
					continue;
				glirps::synthRender(created.synth, glirps::shims::toSpan(remaining.first(frames)));
				remaining = remaining.subspan(frames);
			}
		}
		glirps::synthRelease(created.synth, started.instance);
		return samples;
	}

	// FNV-1a 64 over the samples as little-endian 16-bit PCM.
	std::uint64_t hashPcm16(std::span<const float> samples) noexcept
	{
		constexpr std::uint64_t offset_basis = 0xcbf29ce484222325ULL;
		constexpr std::uint64_t prime = 0x100000001b3ULL;
		constexpr float pcm_scale = 32767.0f;
		constexpr float pcm_min = -32768.0f;

		std::uint64_t hash = offset_basis;
		const auto add = [&hash](std::uint32_t byte) { hash = (hash ^ (byte & 0xffU)) * prime; };
		for (const float sample : samples)
		{
			const float scaled = std::clamp(sample * (pcm_scale / glirps::full_scale), pcm_min, pcm_scale);
			const auto pcm = static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(scaled)));
			add(pcm);
			add(pcm >> 8U);
		}
		return hash;
	}

	std::string toHex(std::uint64_t value)
	{
		std::array<char, 19> text{};
		std::snprintf(text.data(), text.size(), "0x%016llx", static_cast<unsigned long long>(value));
		return text.data();
	}

	class GoldenHashTests : public testing::TestWithParam<GoldenCase>
	{
	};
}

TEST_P(GoldenHashTests, Render_GoldenProgram_MatchesStoredHash)
{
	if (!stored_hashes_apply)
		GTEST_SKIP() << "Golden hashes are checked on Linux only.";

	// ARRANGE
	const GoldenCase& golden = GetParam();

	// ACT
	const std::vector<float> samples = render(golden);

	// ASSERT
	ASSERT_FALSE(samples.empty());
	const std::uint64_t actual = hashPcm16(samples);
	EXPECT_EQ(actual, golden.expected_hash) << "Actual hash of " << golden.name << ": " << toHex(actual);
}

INSTANTIATE_TEST_SUITE_P(Programs,
	GoldenHashTests,
	testing::ValuesIn(golden_cases),
	[](const testing::TestParamInfo<GoldenCase>& info) { return std::string(info.param.name); });

// Guards the golden set itself: a case whose feature has no audible effect, or
// that renders silence, would pin nothing. Platform-independent.
TEST(GoldenProgramTests, Render_AllPrograms_AreAudibleAndDistinct)
{
	// ARRANGE
	std::set<std::uint64_t> hashes;

	for (const GoldenCase& golden : golden_cases)
	{
		SCOPED_TRACE(golden.name);

		// ACT
		const std::vector<float> samples = render(golden);

		// ASSERT
		ASSERT_FALSE(samples.empty());
		EXPECT_GT(glirps::test_support::peakOf(samples), 0.01f);
		hashes.insert(hashPcm16(samples));
	}

	EXPECT_EQ(hashes.size(), golden_cases.size());
}
