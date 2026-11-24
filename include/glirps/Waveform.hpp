#pragma once

/// @file
/// Base voice waveforms.

namespace glirps
{
	/// Base waveform of one voice.
	enum class Waveform
	{
		/// Odd harmonics falling as the square of their order; the softest of
		/// the four.
		triangle,
		/// All harmonics falling as their order; the brightest tonal shape.
		sawtooth,
		/// Rectangular, with harmonic content set by the voice's pulse width.
		pulse,
		/// Pseudo-random, from the voice's seeded LFSR, which the oscillator
		/// phase clocks, so it is pitched rather than white.
		noise,
	};
}
