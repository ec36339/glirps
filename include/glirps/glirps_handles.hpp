#pragma once

/// @file
/// `HandleTraits` specializations for GLIRPS's two opaque
/// handle types, so a consumer can own one via `shims::Handle<T>`
/// instead of calling `synthDestroy`/`blockRendererDestroy` itself at every
/// early-return path.
/// Both deleters are stateless, so `shims::Handle<T>` contributes zero extra
/// bytes over the raw pointer via `[[no_unique_address]]`.
///
/// This header is a separate, optional addition to the library: neither
/// glirps.hpp nor glirps_render.hpp includes it, and neither depends on
/// `Handle.hpp`. A consumer that wants this ownership convenience
/// includes `glirps_handles.hpp` explicitly.
///
/// `shims::Handle` is the RAII owner declared in `Handle.hpp`, and the
/// `HandleTraits` specializations below live in the namespace where that
/// header declares the primary template.

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
