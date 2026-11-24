#pragma once

/// @file
/// Isolates the one owning-resource type of glirps_render.hpp so a consumer
/// that only needs to name `BlockRendererHandle` does not have to include
/// the rest of the BlockRenderer API.

namespace glirps
{
	/// Opaque type; never defined in this header.
	struct BlockRendererObject;
	/// The one owning resource of glirps_render.hpp's BlockRenderer
	/// API: a pointer to `BlockRendererObject`. A null handle is invalid, as is any
	/// handle once passed to `blockRendererDestroy`.
	using BlockRendererHandle = BlockRendererObject*;
}
