#include "ScopedAllocationCounter.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace
{
	// Process-wide allocation counter, incremented by every replaced
	// `operator new` call. Backed by a function-local static so the counter
	// stays mutable without a namespace-scope non-const global.
	std::atomic<std::size_t>& allocationCount() noexcept
	{
		static std::atomic<std::size_t> count = 0;
		return count;
	}
}

namespace glirps::test_support
{
	ScopedAllocationCounter::ScopedAllocationCounter() noexcept
		: baseline_(allocationCount().load(std::memory_order_relaxed))
	{
	}

	std::size_t ScopedAllocationCounter::getAllocationCount() const noexcept
	{
		return allocationCount().load(std::memory_order_relaxed) - baseline_;
	}
}

// The global `operator new`/`operator delete` overrides below exist so
// `ScopedAllocationCounter` can observe process-wide heap allocations from
// test code. They delegate to `malloc`/`free`, which the lint suppressions
// below cover.
// NOLINTBEGIN(cppcoreguidelines-owning-memory, cppcoreguidelines-no-malloc)
void* operator new(std::size_t size)
{
	allocationCount().fetch_add(1, std::memory_order_relaxed);
	void* pointer = std::malloc(size);
	if (pointer == nullptr)
	{
		throw std::bad_alloc();
	}
	return pointer;
}

void* operator new[](std::size_t size) { return ::operator new(size); }

void operator delete(void* pointer) noexcept { std::free(pointer); }

void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

void operator delete[](void* pointer) noexcept { std::free(pointer); }

void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
// NOLINTEND(cppcoreguidelines-owning-memory, cppcoreguidelines-no-malloc)
