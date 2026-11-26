#pragma once

/// @file
/// Note-plus-cents to frequency conversion.

#include "SoundProgram.hpp"

#include <cmath>

namespace glirps
{
	/// Converts a note plus cents to a frequency. `cents` may be fractional.
	/// @param semitone Semitones above C-0.
	/// @param cents Offset from `semitone`, in cents.
	/// @return The frequency in hertz.
	/// @relates VoiceParameters
	[[nodiscard]] inline float frequencyHertz(std::int32_t semitone, float cents) noexcept
	{
		// Tuning reference: concert pitch A-4 and its index on the C-0-based
		// semitone scale.
		constexpr float a4_frequency_hz = 440.0f;
		constexpr float a4_semitone = 57.0f;
		constexpr float semitones_per_octave = 12.0f;
		constexpr float cents_per_semitone = 100.0f;

		const float value = static_cast<float>(semitone) - a4_semitone + cents / cents_per_semitone;
		return a4_frequency_hz * std::pow(2.0f, value / semitones_per_octave);
	}

	/// Converts an unmodified `Pitch` to a frequency.
	/// @param pitch The note and cents offset to convert.
	/// @return The frequency in hertz.
	/// @relates VoiceParameters
	[[nodiscard]] inline float frequencyHertz(const Pitch& pitch) noexcept
	{
		return frequencyHertz(pitch.semitone, static_cast<float>(pitch.cents));
	}
}
