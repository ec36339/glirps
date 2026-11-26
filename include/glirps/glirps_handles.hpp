#pragma once

/// @file
/// Optional `SynthOwner` and `BlockRendererOwner`, which destroy the handle on
/// scope exit. The other headers do not include this one.

#include "BlockRendererHandle.hpp"
#include "SynthHandle.hpp"
#include "glirps.hpp"
#include "glirps_render.hpp"
#include "Handle.hpp"

#include <cassert>

namespace glirps::shims
{
	/// Handle traits for `glirps::SynthHandle`, destroying it with
	/// `glirps::synthDestroy`.
	template<>
	struct HandleTraits<SynthHandle>
	{
		/// Stateless deleter that destroys a synth.
		struct DeleterType
		{
			/// Destroys `h` via `glirps::synthDestroy`.
			/// @param h The synth to destroy; may be null.
			void operator()(glirps::SynthHandle h) const noexcept { glirps::synthDestroy(h); }
		};

		/// @return The null handle value.
		static constexpr glirps::SynthHandle nullValue() noexcept { return nullptr; }
		/// @param h The handle to test.
		/// @return Whether `h` is null.
		static bool isNull(glirps::SynthHandle h) noexcept { return h == nullptr; }
		/// Asserts that `h` is not null.
		/// @param h The handle to check.
		static void assertValid(glirps::SynthHandle h) noexcept { assert(h != nullptr); }
	};

	/// Handle traits for `glirps::BlockRendererHandle`, destroying it with
	/// `glirps::blockRendererDestroy`.
	template<>
	struct HandleTraits<BlockRendererHandle>
	{
		/// Stateless deleter that destroys a block renderer.
		struct DeleterType
		{
			/// Destroys `h` via `glirps::blockRendererDestroy`.
			/// @param h The renderer to destroy; may be null.
			void operator()(glirps::BlockRendererHandle h) const noexcept { glirps::blockRendererDestroy(h); }
		};

		/// @return The null handle value.
		static constexpr glirps::BlockRendererHandle nullValue() noexcept { return nullptr; }
		/// @param h The handle to test.
		/// @return Whether `h` is null.
		static bool isNull(glirps::BlockRendererHandle h) noexcept { return h == nullptr; }
		/// Asserts that `h` is not null.
		/// @param h The handle to check.
		static void assertValid(glirps::BlockRendererHandle h) noexcept { assert(h != nullptr); }
	};
}

namespace glirps
{
	/// Owns a `SynthHandle`, destroying it via `synthDestroy` on scope exit or
	/// reassignment.
	using SynthOwner = shims::Handle<SynthHandle>;

	/// Owns a `BlockRendererHandle`, destroying it via `blockRendererDestroy`
	/// on scope exit or reassignment.
	using BlockRendererOwner = shims::Handle<BlockRendererHandle>;
}
