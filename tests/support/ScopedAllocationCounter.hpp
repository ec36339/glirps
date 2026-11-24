#pragma once

#include <cstddef>

namespace glirps::test_support
{
	// Counts heap allocations for the lifetime of the scope, by reading a
	// process-wide counter the global `operator new` overloads maintain
	// (defined in `ScopedAllocationCounter.cpp`, linked into any target that
	// needs this). The overloads run for the whole process once linked;
	// this type only reports the delta across its own lifetime, so multiple
	// instances (nested or sequential) are safe.
	//
	// Before using this, consider whether the component under test could take
	// a custom allocator interface instead: a scoped allocator is precise and
	// supports exact-count assertions. This process-wide override is
	// acceptable at least for "allocates nothing" assertions.
	class ScopedAllocationCounter
	{
	public:
		ScopedAllocationCounter() noexcept;

		// Returns allocations observed since construction.
		[[nodiscard]] std::size_t getAllocationCount() const noexcept;

	private:
		std::size_t baseline_;
	};
}
