#pragma once

#include "glirps/glirps.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The golden programs: fixed sound programs that together cover the features of
// the synth, and how they are rendered and hashed.
namespace glirps::test_support
{
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
		pitch_offset_step,
		pitch_offset_linear,
		pitch_offset_catmull_rom,
		pulse_width_delta,
		resonance_table,
	};

	struct GoldenCase
	{
		std::string_view name;
		Waveform waveform = Waveform::pulse;
		Feature feature = Feature::none;
		float grit_scale = 1.0f;
		std::uint64_t seed = 1;
		std::uint32_t onset_delay_base_ticks = 0;
		std::uint32_t tick_multiplier = 1;
		std::uint64_t expected_hash = 0;
	};

	inline void PrintTo(const GoldenCase& golden, std::ostream* stream) { *stream << golden.name; }

	inline constexpr std::array golden_cases = {
		GoldenCase{.name = "triangle", .waveform = Waveform::triangle, .expected_hash = 0x256eb3e210941382ULL},
		GoldenCase{.name = "sawtooth", .waveform = Waveform::sawtooth, .expected_hash = 0xcacf34b173303f31ULL},
		GoldenCase{.name = "pulse", .expected_hash = 0xcfe11c92e932715bULL},
		GoldenCase{.name = "noise", .waveform = Waveform::noise, .expected_hash = 0x930ce9d13ae18ddbULL},
		GoldenCase{.name = "ring_modulation",
			.waveform = Waveform::triangle,
			.feature = Feature::ring_modulation,
			.expected_hash = 0x006fbcb363e284f2ULL},
		GoldenCase{.name = "hard_sync",
			.waveform = Waveform::sawtooth,
			.feature = Feature::hard_sync,
			.expected_hash = 0x9205e60b1194afa7ULL},
		GoldenCase{.name = "filter_lowpass",
			.waveform = Waveform::sawtooth,
			.feature = Feature::lowpass,
			.expected_hash = 0x0144da1fc5c62edaULL},
		GoldenCase{.name = "filter_bandpass",
			.waveform = Waveform::sawtooth,
			.feature = Feature::bandpass,
			.expected_hash = 0x463d5a067bff5888ULL},
		GoldenCase{.name = "filter_highpass",
			.waveform = Waveform::sawtooth,
			.feature = Feature::highpass,
			.expected_hash = 0xcbbf08ca1efb86cfULL},
		GoldenCase{.name = "filter_notch",
			.waveform = Waveform::sawtooth,
			.feature = Feature::notch,
			.expected_hash = 0x8b7eb77263f0a6a2ULL},
		GoldenCase{.name = "tables", .feature = Feature::tables, .expected_hash = 0x378e0432f366f411ULL},
		GoldenCase{.name = "onset_delay", .onset_delay_base_ticks = 5, .expected_hash = 0x1a4cb7368962573bULL},
		// Seed 1, the default, draws no pitch step; this one draws a different one.
		GoldenCase{.name = "variation_seed", .seed = 2, .expected_hash = 0xc761b08b9ed36c87ULL},
		GoldenCase{.name = "grit_scale_zero", .grit_scale = 0.0f, .expected_hash = 0xef119eabf2dff12eULL},
		GoldenCase{.name = "grit_scale_half", .grit_scale = 0.5f, .expected_hash = 0xce57a0a02a1e19afULL},
		GoldenCase{.name = "pitch_offset_step",
			.feature = Feature::pitch_offset_step,
			.expected_hash = 0xd696600860d546f2ULL},
		GoldenCase{.name = "pitch_offset_linear",
			.feature = Feature::pitch_offset_linear,
			.expected_hash = 0xc6fdcb4f24df6ad9ULL},
		GoldenCase{.name = "pitch_offset_catmull_rom",
			.feature = Feature::pitch_offset_catmull_rom,
			.expected_hash = 0x7f222408240a124eULL},
		// As `pitch_offset_linear`, with an effective tick four times as often.
		GoldenCase{.name = "tick_multiplier",
			.feature = Feature::pitch_offset_linear,
			.tick_multiplier = 4,
			.expected_hash = 0x54b5441a8f3ab843ULL},
		GoldenCase{.name = "pulse_width_delta",
			.feature = Feature::pulse_width_delta,
			.expected_hash = 0xc78942ae5b448f76ULL},
		// As `filter_lowpass`, with an automated resonance.
		GoldenCase{.name = "resonance_table",
			.waveform = Waveform::sawtooth,
			.feature = Feature::resonance_table,
			.expected_hash = 0x7c5c465f862a86e6ULL},
	};

	// Why a rendering stopped: the error of the call that failed.
	using RenderError = std::variant<CreateError, StartError>;

	// Names the failed call and its error value, for a failure message.
	[[nodiscard]] std::string describe(const RenderError& error);

	// Renders `golden` for a fixed duration. Blocks of one base tick are spaced
	// evenly over its sub-blocks.
	[[nodiscard]] std::expected<std::vector<float>, RenderError> renderGoldenCase(const GoldenCase& golden);

	// FNV-1a 64 over the samples as little-endian 16-bit PCM.
	[[nodiscard]] std::uint64_t hashPcm16(std::span<const float> samples) noexcept;
}
