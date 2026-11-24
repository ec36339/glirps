#pragma once

#include "glirps/Environment.hpp"

#include <cassert>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace glirps::impl
{
	// A copy of the consumer's allocator callbacks, or the default C++
	// allocation when the consumer supplied none. Every heap allocation of the
	// library goes through one of these.
	class AllocatorRef
	{
	public:
		AllocatorRef() noexcept = default;

		// Selects `callbacks` when both function pointers are set. A partial
		// pair selects the default allocation and asserts in debug builds.
		explicit AllocatorRef(const AllocatorCallbacks& callbacks) noexcept
		{
			const bool complete = callbacks.allocate != nullptr && callbacks.deallocate != nullptr;
			assert((complete || (callbacks.allocate == nullptr && callbacks.deallocate == nullptr))
				&& "AllocatorCallbacks must have both function pointers set or none");
			if (complete)
				callbacks_ = callbacks;
		}

		// Returns `size` bytes aligned to `alignment`. Throws `std::bad_alloc`
		// when no memory is available, so the create functions can report an
		// allocation failure from any depth of construction.
		[[nodiscard]] void* allocate(std::size_t size, std::size_t alignment) const
		{
			if (callbacks_.allocate == nullptr)
				return isOverAligned(alignment) ? ::operator new(size, std::align_val_t{alignment})
												: ::operator new(size);
			void* memory = callbacks_.allocate(callbacks_.context, size, alignment);
			if (memory == nullptr)
				throw std::bad_alloc();
			return memory;
		}

		// Releases memory that `allocate` returned for the same `size` and `alignment`.
		void deallocate(void* memory, std::size_t size, std::size_t alignment) const noexcept
		{
			if (callbacks_.deallocate != nullptr)
				callbacks_.deallocate(callbacks_.context, memory, size, alignment);
			else if (isOverAligned(alignment))
				::operator delete(memory, size, std::align_val_t{alignment});
			else
				::operator delete(memory, size);
		}

		// Two references are equal when they use the same callbacks and context.
		[[nodiscard]] friend bool operator==(const AllocatorRef& left, const AllocatorRef& right) noexcept
		{
			return left.callbacks_.context == right.callbacks_.context
				&& left.callbacks_.allocate == right.callbacks_.allocate
				&& left.callbacks_.deallocate == right.callbacks_.deallocate;
		}

	private:
		// Reports whether the default allocation needs the aligned overloads.
		[[nodiscard]] static constexpr bool isOverAligned(std::size_t alignment) noexcept
		{
			return alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__;
		}

		AllocatorCallbacks callbacks_;
	};

	// A standard allocator over an `AllocatorRef`, for the standard containers
	// inside the library. Two allocators are equal when they use the same
	// callbacks and context, so a container's storage can be released by any
	// copy of its allocator.
	template<typename T>
	class Allocator
	{
	public:
		using value_type = T;
		using propagate_on_container_copy_assignment = std::true_type;
		using propagate_on_container_move_assignment = std::true_type;
		using propagate_on_container_swap = std::true_type;

		Allocator() noexcept = default;

		// Allocates through `allocator`.
		explicit Allocator(const AllocatorRef& allocator) noexcept
			: allocator_(allocator)
		{
		}

		// Rebinds `other` to another value type, keeping its allocation.
		template<typename U>
		Allocator(const Allocator<U>& other) noexcept
			: allocator_(other.getAllocatorRef())
		{
		}

		// Returns storage for `count` objects. Throws `std::bad_alloc` when none is available.
		[[nodiscard]] T* allocate(std::size_t count)
		{
			if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
				throw std::bad_alloc();
			return static_cast<T*>(allocator_.allocate(count * sizeof(T), alignof(T)));
		}

		// Releases storage that `allocate` returned for the same `count`.
		void deallocate(T* memory, std::size_t count) noexcept
		{
			allocator_.deallocate(memory, count * sizeof(T), alignof(T));
		}

		// Returns the allocation this allocator forwards to.
		[[nodiscard]] const AllocatorRef& getAllocatorRef() const noexcept { return allocator_; }

		// Two allocators are equal when their allocations are equal.
		template<typename U>
		[[nodiscard]] bool operator==(const Allocator<U>& other) const noexcept
		{
			return allocator_ == other.getAllocatorRef();
		}

	private:
		AllocatorRef allocator_;
	};

	// Destroys `object` and releases its storage through `allocator`. Null-safe.
	template<typename T>
	void destroyAndDeallocate(T* object, const AllocatorRef& allocator) noexcept
	{
		if (object == nullptr)
			return;
		std::destroy_at(object);
		allocator.deallocate(object, sizeof(T), alignof(T));
	}

	// Deleter of `UniquePtr`: holds the allocator the object came from.
	template<typename T>
	class Deleter
	{
	public:
		Deleter() noexcept = default;

		// Releases objects through `allocator`.
		explicit Deleter(const AllocatorRef& allocator) noexcept
			: allocator_(allocator)
		{
		}

		// Destroys `object` and releases its storage. Null-safe.
		void operator()(T* object) const noexcept { destroyAndDeallocate(object, allocator_); }

	private:
		AllocatorRef allocator_;
	};

	// An owning pointer to an object allocated through an `AllocatorRef`.
	template<typename T>
	using UniquePtr = std::unique_ptr<T, Deleter<T>>;

	// Allocates storage through `allocator` and constructs a `T` in it. Throws
	// whatever the allocation or the constructor throws; storage is released if
	// the constructor throws.
	template<typename T, typename... Args>
	[[nodiscard]] UniquePtr<T> makeUnique(const AllocatorRef& allocator, Args&&... args)
	{
		void* memory = allocator.allocate(sizeof(T), alignof(T));
		try
		{
			T* object = std::construct_at(static_cast<T*>(memory), std::forward<Args>(args)...);
			return UniquePtr<T>(object, Deleter<T>(allocator));
		}
		catch (...)
		{
			allocator.deallocate(memory, sizeof(T), alignof(T));
			throw;
		}
	}
}
