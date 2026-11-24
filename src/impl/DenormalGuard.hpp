#pragma once

namespace glirps::impl
{
	// Scoped flush-to-zero and denormals-are-zero guard. Enables both modes on
	// construction and restores the caller's previous mode on destruction, so
	// denormal arithmetic does not stall the code it wraps. `Synth::render`
	// holds one for its duration. The modes are x86 SSE controls; the
	// implementation lives in the corresponding `.cpp` so this header is
	// portable. On platforms without those controls the guard is a no-op.
	class DenormalGuard
	{
	public:
		DenormalGuard() noexcept;
		~DenormalGuard();

		DenormalGuard(const DenormalGuard&) = delete;
		DenormalGuard& operator=(const DenormalGuard&) = delete;
		DenormalGuard(DenormalGuard&&) = delete;
		DenormalGuard& operator=(DenormalGuard&&) = delete;

	private:
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
		unsigned int previous_flush_mode_;
		unsigned int previous_denormals_mode_;
#endif
	};
}
