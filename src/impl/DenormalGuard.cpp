#include "DenormalGuard.hpp"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include <pmmintrin.h>
#include <xmmintrin.h>
#endif

namespace glirps::impl
{
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
	DenormalGuard::DenormalGuard() noexcept
		: previous_flush_mode_(_MM_GET_FLUSH_ZERO_MODE())
		, previous_denormals_mode_(_MM_GET_DENORMALS_ZERO_MODE())
	{
		_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
		_MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
	}

	DenormalGuard::~DenormalGuard()
	{
		_MM_SET_FLUSH_ZERO_MODE(previous_flush_mode_);
		_MM_SET_DENORMALS_ZERO_MODE(previous_denormals_mode_);
	}
#else
	// No portable controls for flush-to-zero or denormals-are-zero exist in
	// standard C++; on platforms without the x86 SSE intrinsics the guard is
	// a no-op. Denormals here are a performance hazard, not a correctness
	// one, so running unflushed is acceptable.
	DenormalGuard::DenormalGuard() noexcept = default;
	DenormalGuard::~DenormalGuard() = default;
#endif
}
