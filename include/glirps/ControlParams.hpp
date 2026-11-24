#pragma once

/// @file
/// External control flags and parameters for a playing instance.

#include <cstdint>

namespace glirps
{
	/// The flags that name which fields of a `ControlParams` a call carries.
	/// Declared as `inline static constexpr` members of a struct rather than an
	/// `enum class` so the flags OR together without a cast operator, and rather
	/// than namespace-scope constants so they carry a scope name at the call
	/// site: `ControlFlag::cutoff_hz`.
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
	/// applied only when its flag is set in `flags`; an unflagged field keeps
	/// the instance's current value, so a field's "not overridden" state is
	/// explicit rather than encoded in a sentinel. `reset_flags` names fields
	/// `synthUpdateControlParams` instead resets to their authored state; a
	/// field set in both `flags` and `reset_flags` resets, and `flags`'s value
	/// for that field is ignored. `synthStart` does not read `reset_flags`.
	struct ControlParams
	{
		/// OR-ed `ControlFlag` bits naming which fields this call carries.
		std::uint32_t flags = 0;
		/// OR-ed `ControlFlag` bits naming which fields `synthUpdateControlParams`
		/// resets to their authored state, taking precedence over `flags` for
		/// the same field. Not read by `synthStart`.
		std::uint32_t reset_flags = 0;
		/// Pitch offset in cents added to the instance's assembled pitch.
		float pitch_cents = 0.0f;
		/// Filter cutoff override in hertz. A flagged zero overrides to the
		/// cutoff floor rather than restoring the authored cutoff;
		/// `reset_flags` restores the authored cutoff.
		float cutoff_hz = 0.0f;
		/// Linear gain multiplier on the instance's chip output gain. A flagged
		/// 1 leaves the level unchanged, 0 silences it without releasing the gate.
		float gain_multiplier = 1.0f;
	};
}
