#pragma once

/// @file
/// Declares `SynthHandle` without the rest of the synth API.

namespace glirps
{
	/// Opaque type; never defined in this header.
	struct SynthObject;
	/// The one owning resource of glirps.hpp's Synth API: a pointer to
	/// `SynthObject`. A null handle is invalid, as is any handle
	/// once passed to `synthDestroy`.
	using SynthHandle = SynthObject*;
}
