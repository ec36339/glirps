#pragma once

/// @file
/// Per-effective-tick parameter blocks and their value ranges.

#include "Array.hpp"
#include "ChipInstanceHandle.hpp"
#include "FilterMode.hpp"
#include "Waveform.hpp"
#include "constants.hpp"

#include <cstdint>

namespace glirps
{
	// Legal ranges of `ParameterBlock` fields.

	/// Lowest legal oscillator frequency.
	inline constexpr float frequency_hz_floor = 0.0f;
	/// Lowest legal gain.
	inline constexpr float gain_floor = 0.0f;
	/// Upper end of the authored gain range (unity gain).
	inline constexpr float gain_authored_ceiling = 1.0f;
	/// Highest gain a runtime clamp lets through.
	inline constexpr float gain_ceiling = 1000.0f;
	/// Lowest legal pulse width.
	inline constexpr float pulse_width_floor = 0.0f;
	/// Highest legal pulse width.
	inline constexpr float pulse_width_ceiling = 1.0f;
	/// Lowest legal filter resonance Q.
	inline constexpr float resonance_q_floor = 0.5f;
	/// Highest legal filter resonance Q.
	inline constexpr float resonance_q_ceiling = 20.0f;

	/// One voice's evaluated parameters for one effective tick.
	struct VoiceParameters
	{
		/// Waveform this voice renders for this tick.
		Waveform waveform = Waveform::pulse;
		/// Oscillator frequency, from `frequency_hz_floor` up to Nyquist.
		float frequency_hz = 0.0f;
		/// Duty cycle of a pulse waveform, in `[pulse_width_floor, pulse_width_ceiling]`. Read only under
		/// `Waveform::pulse`, but always carries a value in range.
		float pulse_width = 0.5f;
		/// Voice gain, in `[0, gain_ceiling]`; authored values do not exceed
		/// `gain_authored_ceiling`.
		float gain = 1.0f;
		/// Whether this voice's envelope is gated on for this tick.
		bool gate_on = false;
	};

	/// The evaluated and quantized parameters of one chip instance at one
	/// effective tick. Produced by @ref synthEvaluateTick, consumed by @ref synthApplyBlock,
	/// @ref blockRendererApplyBlock, and @ref blockRendererApplyBaseBlock. Trivially copyable, so a consumer may
	/// carry it across a thread boundary by value with no allocation.
	struct ParameterBlock
	{
		/// The chip instance this block targets.
		ChipInstanceHandle target;
		/// Shared filter responses for this tick, summed. All four false passes
		/// the input through unfiltered.
		FilterModeSet filter_modes;
		/// Shared filter cutoff in hertz. @ref synthEvaluateTick clamps it to 1 Hz up to
		/// just under half the sample rate.
		float cutoff_hz = 20000.0f;
		/// Shared filter resonance, in `[resonance_q_floor, resonance_q_ceiling]`.
		float resonance_q = 0.707f;
		/// Chip-wide output gain, in `[0, gain_ceiling]`; authored values do not
		/// exceed `gain_authored_ceiling`.
		float output_gain = 1.0f;
		/// Entries in `[0, voice_count)` of `voices` are meaningful.
		std::uint32_t voice_count = 0;
		/// Per-voice parameters for this tick.
		shims::Array<VoiceParameters, max_voices_per_instance> voices;
	};

	/// The time span of one block, as indices of sub-blocks
	/// (`sub_block_frame_count` frames each) counted from the instance's first
	/// block. Reported by @ref synthEvaluateTick.
	struct TickSubBlockSpan
	{
		/// Sub-block containing this tick's own start.
		std::uint64_t start = 0;
		/// Sub-block containing the next tick's start, which is this tick's natural
		/// end.
		std::uint64_t next_start = 0;
	};
}
