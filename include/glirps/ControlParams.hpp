#pragma once

/// @file
/// External control flags and parameters for a playing instance.

#include <cstdint>

namespace glirps
{
	/// The flags that name which fields of a `ControlParams` a call carries.
	struct ControlFlag
	{
		/// `pitch_cents` is applied: added to the instance's assembled pitch.
		inline static constexpr std::uint32_t pitch_cents = 1U << 0;
		/// `cutoff_hz` is applied: overrides the authored filter cutoff.
		inline static constexpr std::uint32_t cutoff_hz = 1U << 1;
		/// `gain_multiplier` is applied: scales the chip output gain.
		inline static constexpr std::uint32_t gain_multiplier = 1U << 2;
	};

	/// The flagged external-control aggregate both `synthStart` and
	/// `synthUpdateControlParams` accept. Trivially copyable and
	/// standard-layout, so it crosses the public boundary by value. A field is
	/// applied only if its flag is set in `flags`; otherwise its current value is
	/// kept (at start, the authored one). A flagged non-finite value is ignored.
	/// `synthUpdateControlParams` restores fields named in `reset_flags` to their
	/// authored state, even if also flagged; `synthStart` ignores `reset_flags`.
	struct ControlParams
	{
		/// `ControlFlag` bits naming the fields to apply.
		std::uint32_t flags = 0;
		/// `ControlFlag` bits naming the fields to restore.
		std::uint32_t reset_flags = 0;
		/// Pitch offset in cents added to the instance's assembled pitch.
		float pitch_cents = 0.0f;
		/// Filter cutoff override in hertz. A flagged zero overrides to the
		/// cutoff floor rather than restoring the authored cutoff.
		float cutoff_hz = 0.0f;
		/// Multiplies the program's `output_gain`; a new value replaces the
		/// previous multiplier. A flagged 1 leaves the level unchanged, 0 silences
		/// it without releasing the gate.
		float gain_multiplier = 1.0f;
	};
}
