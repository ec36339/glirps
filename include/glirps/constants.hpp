#pragma once

/// @file
/// Library-wide constants.

#include <cstdint>

namespace glirps
{
	/// Nominal full scale of the float sample path. The path may exceed this
	/// internally; only the consumer's output stage constrains the range.
	inline constexpr float full_scale = 1.0f;

	/// Nominal peak level of one unfiltered voice at full waveform amplitude with
	/// every gain at 1.0. Gains are linear multipliers relative to this level.
	inline constexpr float nominal_source_level_db = -12.0f;

	/// Decibel amplitude scale factor: level_dbfs = decibel_amplitude_scale *
	/// log10(linear).
	inline constexpr float decibel_amplitude_scale = 20.0f;

	/// Base ticks per second.
	inline constexpr std::uint32_t control_grid_base_rate_hz = 50;

	/// Subticks per base tick. The subtick is the unit of every time
	/// in a `SoundProgram`. With a power-of-two `tick_multiplier` up to this
	/// value, every effective tick lands on a whole subtick.
	inline constexpr std::uint32_t subticks_per_base_tick = 16;

	/// Render granularity. A parameter smoothing ramp completes in exactly one
	/// sub-block.
	inline constexpr std::uint32_t sub_block_frame_count = 64;

	/// Default oversampling factor.
	inline constexpr std::uint32_t default_oversampling_factor = 16U;

	/// Upper bound on the voices one chip instance may hold.
	inline constexpr std::uint32_t max_voices_per_instance = 4;
}
