#pragma once

/// @file
/// Reasons a sound can fail to start.

namespace glirps
{
	/// Why `synthStart` could not begin a sound. Trivially copyable and
	/// carrying no diagnostic payload.
	enum class StartError
	{
		/// No chip instance slot is free. Expected and recoverable.
		chip_instances_exhausted,
		/// Chip instance slots remain, but not one voice slot per authored voice.
		voices_exhausted,
		/// The program is malformed, for example it declares no voices or more
		/// than `max_voices_per_instance`.
		invalid_program,
	};
}
