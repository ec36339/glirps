#pragma once

/// @file
/// Declares `BlockRendererHandle` without the rest of the block renderer API.

namespace glirps
{
	/// Opaque type; never defined in this header.
	struct BlockRendererObject;
	/// The one owning resource of glirps_render.hpp's BlockRenderer
	/// API: a pointer to `BlockRendererObject`. A null handle is invalid, as is any
	/// handle once passed to `blockRendererDestroy`.
	using BlockRendererHandle = BlockRendererObject*;
}
