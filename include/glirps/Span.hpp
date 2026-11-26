#pragma once

/// @file
/// Non-owning view type used at the library boundary.

#include <cassert>
#include <cstddef>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>

namespace glirps::shims
{
	/// A non-owning view of `size` contiguous `T` values starting at `data`.
	/// Stands in for `std::span`: the layout is exactly a pointer followed by a
	/// `std::size_t` count, which the standard does not guarantee for
	/// `std::span`. It offers a subset of `std::span`'s interface plus `at()`;
	/// the members it has match `std::span` in name and semantics.
	/// Standard-layout and trivially copyable.
	/// @ingroup shims
	template<typename T>
	class Span
	{
	public:
		/// Creates an empty view with a null `data()`.
		constexpr Span() noexcept = default;

		/// Creates a view of `size` values starting at `data`. `data` must
		/// point to at least `size` values, or be null when `size` is zero.
		/// @param data Pointer to the first value.
		/// @param size Number of values in the view.
		constexpr Span(T* data, std::size_t size) noexcept
			: data_(data)
			, size_(size)
		{
		}

		/// Converts a view of non-const values to a view of const values.
		/// @param other The view to convert.
		template<typename U>
			requires(std::is_same_v<T, const U> && !std::is_const_v<U>)
		constexpr Span(const Span<U>& other) noexcept
			: data_(other.data())
			, size_(other.size())
		{
		}

		/// Creates a view of the same values as `view`.
		/// @param view The `std::span` to view.
		template<std::size_t Extent>
		constexpr explicit Span(std::span<T, Extent> view) noexcept
			: data_(view.data())
			, size_(view.size())
		{
		}

		/// @return A `std::span` over the same values.
		constexpr explicit operator std::span<T>() const noexcept { return std::span<T>(data_, size_); }

		/// @return Pointer to the first value, or null for a default-constructed view.
		[[nodiscard]] constexpr T* data() const noexcept { return data_; }

		/// @return The number of values in the view.
		[[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

		/// @return True when the view holds no values.
		[[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

		/// Returns the value at `index`. `index` must be less than `size()`;
		/// debug builds assert it.
		/// @param index Position of the value.
		/// @return Reference to the value at `index`.
		[[nodiscard]] constexpr T& operator[](std::size_t index) const noexcept
		{
			assert(index < size_);
			return data_[index];
		}

		/// Returns the value at `index`, throwing `std::out_of_range` when
		/// `index` is not less than `size()`. Not usable from `noexcept` code.
		/// @param index Position of the value.
		/// @return Reference to the value at `index`.
		[[nodiscard]] constexpr T& at(std::size_t index) const
		{
			if (index >= size_)
			{
				throw std::out_of_range("Span::at: index out of range");
			}
			return data_[index];
		}

		/// Returns the first value. The view must not be empty; debug builds
		/// assert it.
		/// @return Reference to the first value.
		[[nodiscard]] constexpr T& front() const noexcept
		{
			assert(size_ > 0);
			return data_[0];
		}

		/// Returns the last value. The view must not be empty; debug builds
		/// assert it.
		/// @return Reference to the last value.
		[[nodiscard]] constexpr T& back() const noexcept
		{
			assert(size_ > 0);
			return data_[size_ - 1];
		}

		/// @return Pointer to the first value.
		[[nodiscard]] constexpr T* begin() const noexcept { return data_; }

		/// @return Pointer one past the last value.
		[[nodiscard]] constexpr T* end() const noexcept { return data_ + size_; }

	private:
		T* data_ = nullptr;
		std::size_t size_ = 0;
	};

	/// Deduces `Span<T>` from a `std::span<T, Extent>`.
	template<typename T, std::size_t Extent>
	Span(std::span<T, Extent>) -> Span<T>;

	/// Views the contiguous storage of `range` as a `Span`, deducing the
	/// element type: `toSpan(vector)` gives a `Span<T>`, and `toSpan(const_vector)`
	/// gives a `Span<const T>`. A temporary container is rejected, because the
	/// view would dangle; a temporary view such as `std::span` is accepted.
	/// @param range The contiguous range to view.
	/// @return A view of the range's storage.
	/// @relates Span
	/// @ingroup shims
	template<std::ranges::contiguous_range Range>
		requires(std::ranges::sized_range<Range>
			&& (std::is_lvalue_reference_v<Range> || std::ranges::borrowed_range<Range>))
	[[nodiscard]] constexpr auto toSpan(Range&& range) noexcept
	{
		using Element = std::remove_pointer_t<decltype(std::ranges::data(range))>;
		return Span<Element>(std::ranges::data(range), std::ranges::size(range));
	}
}
