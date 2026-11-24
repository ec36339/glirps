#pragma once

/// @file
/// Fixed-size array type used at the library boundary.

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <stdexcept>

namespace glirps::shims
{
	/// @defgroup shims C++ Shims
	/// Layout-guaranteed stand-ins for standard library types, and the RAII
	/// `Handle` wrapper.

	/// A fixed-size array of `N` `T` values, an aggregate over `T[N]` so brace
	/// initialization works as for `std::array`. Stands in for `std::array`:
	/// the layout is exactly `T[N]`, which the standard does not guarantee for
	/// `std::array`. It offers a subset of `std::array`'s interface; the
	/// members it has match `std::array` in name and semantics. Standard-layout
	/// and trivially copyable when `T` is.
	/// @ingroup shims
	template<typename T, std::size_t N>
	struct Array
	{
		static_assert(N > 0, "Array requires at least one element.");

		/// The stored values. Public so that the type stays an aggregate.
		T elements[N];

		/// Creates an `Array` holding a copy of each value of `source`.
		/// @param source The values to copy.
		/// @return An `Array` with the same values.
		[[nodiscard]] static constexpr Array fromStdArray(const std::array<T, N>& source)
		{
			Array result{};
			std::copy(source.begin(), source.end(), result.elements);
			return result;
		}

		/// Returns a `std::array` holding a copy of each value.
		/// @return A `std::array` holding a copy of each value.
		[[nodiscard]] constexpr explicit operator std::array<T, N>() const
		{
			std::array<T, N> result{};
			std::copy(elements, elements + N, result.begin());
			return result;
		}

		/// Returns a pointer to the first value.
		/// @return Pointer to the first value.
		[[nodiscard]] constexpr T* data() noexcept { return elements; }

		/// Returns a pointer to the first value.
		/// @return Pointer to the first value.
		[[nodiscard]] constexpr const T* data() const noexcept { return elements; }

		/// Returns the number of values, `N`.
		/// @return The number of values.
		[[nodiscard]] constexpr std::size_t size() const noexcept { return N; }

		/// Returns the value at `index`. `index` SHALL be less than `N`; debug
		/// builds assert it.
		/// @param index Position of the value.
		/// @return Reference to the value at `index`.
		[[nodiscard]] constexpr T& operator[](std::size_t index) noexcept
		{
			assert(index < N);
			return elements[index];
		}

		/// Returns the value at `index`. `index` SHALL be less than `N`; debug
		/// builds assert it.
		/// @param index Position of the value.
		/// @return Reference to the value at `index`.
		[[nodiscard]] constexpr const T& operator[](std::size_t index) const noexcept
		{
			assert(index < N);
			return elements[index];
		}

		/// Returns the value at `index`, throwing `std::out_of_range` when
		/// `index` is not less than `N`. Not usable from `noexcept` code.
		/// @param index Position of the value.
		/// @return Reference to the value at `index`.
		[[nodiscard]] constexpr T& at(std::size_t index)
		{
			if (index >= N)
			{
				throw std::out_of_range("Array::at: index out of range");
			}
			return elements[index];
		}

		/// Returns the value at `index`, throwing `std::out_of_range` when
		/// `index` is not less than `N`. Not usable from `noexcept` code.
		/// @param index Position of the value.
		/// @return Reference to the value at `index`.
		[[nodiscard]] constexpr const T& at(std::size_t index) const
		{
			if (index >= N)
			{
				throw std::out_of_range("Array::at: index out of range");
			}
			return elements[index];
		}

		/// Returns the first value.
		/// @return Reference to the first value.
		[[nodiscard]] constexpr T& front() noexcept { return elements[0]; }

		/// Returns the first value.
		/// @return Reference to the first value.
		[[nodiscard]] constexpr const T& front() const noexcept { return elements[0]; }

		/// Returns the last value.
		/// @return Reference to the last value.
		[[nodiscard]] constexpr T& back() noexcept { return elements[N - 1]; }

		/// Returns the last value.
		/// @return Reference to the last value.
		[[nodiscard]] constexpr const T& back() const noexcept { return elements[N - 1]; }

		/// Returns a pointer to the first value.
		/// @return Pointer to the first value.
		[[nodiscard]] constexpr T* begin() noexcept { return elements; }

		/// Returns a pointer to the first value.
		/// @return Pointer to the first value.
		[[nodiscard]] constexpr const T* begin() const noexcept { return elements; }

		/// Returns a pointer one past the last value.
		/// @return Pointer one past the last value.
		[[nodiscard]] constexpr T* end() noexcept { return elements + N; }

		/// Returns a pointer one past the last value.
		/// @return Pointer one past the last value.
		[[nodiscard]] constexpr const T* end() const noexcept { return elements + N; }

		/// Assigns `value` to every element.
		/// @param value The value to store in each element.
		constexpr void fill(const T& value) { std::fill(elements, elements + N, value); }
	};
}
