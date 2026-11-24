#pragma once

/// @file
/// Isolates the one owning-resource type of glirps.hpp so a consumer that
/// only needs to name `SynthHandle` (e.g. in a member declaration or a
/// forward-declared signature) does not have to include the rest of the
/// Synth API.

namespace glirps
{
	/// Opaque type; never defined in this header.
	struct SynthObject;
	/// The one owning resource of glirps.hpp's Synth API: a pointer to
	/// `SynthObject`. A null handle is invalid, as is any handle
	/// once passed to `synthDestroy`.
	using SynthHandle = SynthObject*;
}
