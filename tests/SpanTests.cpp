#include "glirps/Span.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
	constexpr std::array<int, 4> storage{{4, 5, 6, 7}};
	constexpr glirps::shims::Span<const int> view(storage.data(), storage.size());

	constexpr int sumOf(glirps::shims::Span<const int> values)
	{
		int total = 0;
		for (const int value : values)
		{
			total += value;
		}
		return total;
	}

	template<typename Range>
	concept CanMakeSpan = requires(Range&& range) { glirps::shims::toSpan(std::forward<Range>(range)); };

	constexpr int writeThroughMutableView()
	{
		std::array<int, 2> values{{5, 6}};
		const glirps::shims::Span<int> mutable_view(values.data(), values.size());
		mutable_view.at(1) = 20;
		mutable_view.front() = mutable_view.front() + 1;
		return values.front() * 100 + values.back();
	}

	constexpr int firstOfConstConversion()
	{
		std::array<int, 2> values{{5, 6}};
		const glirps::shims::Span<int> mutable_view(values.data(), values.size());
		const glirps::shims::Span<const int> const_view = mutable_view;
		return const_view.front();
	}

	constexpr int firstOfStdSpanRoundTrip()
	{
		std::array<int, 2> values{{5, 6}};
		const std::span<int> standard_view(values);
		const glirps::shims::Span<int> converted(standard_view);
		const std::span<int> back = static_cast<std::span<int>>(converted);
		return back.front();
	}
}

// `Span` mirrors the `std::span` members it has. All of it is `constexpr`, so
// the checks below are evaluated at compile time.
static_assert(glirps::shims::Span<float>{}.empty());
static_assert(glirps::shims::Span<float>{}.size() == 0);
static_assert(glirps::shims::Span<float>{}.data() == nullptr);
static_assert(!view.empty());
static_assert(view.size() == 4);
static_assert(view.data() == storage.data());
// `operator[]` is the unit under test here, so the unchecked-access lint is
// disabled for this one line; every other access in this file uses `at()`.
// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
static_assert(view[2] == 6);
static_assert(view.at(3) == 7);
static_assert(view.front() == 4);
static_assert(view.back() == 7);
static_assert(view.end() - view.begin() == 4);
static_assert(sumOf(view) == 22);
static_assert(writeThroughMutableView() == 620);
static_assert(firstOfConstConversion() == 5);
static_assert(firstOfStdSpanRoundTrip() == 5);

// `Span<T>` converts implicitly to `Span<const T>` only; conversions to and
// from `std::span` are explicit.
static_assert(std::is_convertible_v<glirps::shims::Span<int>, glirps::shims::Span<const int>>);
static_assert(!std::is_convertible_v<glirps::shims::Span<const int>, glirps::shims::Span<int>>);
static_assert(!std::is_convertible_v<std::span<float>, glirps::shims::Span<float>>);
static_assert(!std::is_convertible_v<glirps::shims::Span<float>, std::span<float>>);

// `toSpan` deduces the element type from the range, keeps constness, and
// rejects a temporary container whose view would dangle.
static_assert(std::is_same_v<decltype(glirps::shims::toSpan(storage)), glirps::shims::Span<const int>>);
static_assert(glirps::shims::toSpan(storage).size() == 4);
static_assert(glirps::shims::toSpan(storage).back() == 7);
static_assert(std::is_same_v<decltype(glirps::shims::toSpan(std::span<float>{})), glirps::shims::Span<float>>);
static_assert(CanMakeSpan<std::vector<int>&>);
static_assert(!CanMakeSpan<std::vector<int>>);

// Deduction guide from `std::span`.
static_assert(
	std::is_same_v<decltype(glirps::shims::Span(std::span<const int>(storage))), glirps::shims::Span<const int>>);

TEST(SpanTests, At_IndexNotLessThanSize_ThrowsOutOfRange)
{
	// ARRANGE
	std::vector<int> values{1, 2, 3};
	const glirps::shims::Span<int> values_view = glirps::shims::toSpan(values);

	// ACT & ASSERT
	EXPECT_THROW(std::ignore = values_view.at(3), std::out_of_range);
}

TEST(SpanTests, ToSpan_Vector_WritesThroughToVector)
{
	// ARRANGE
	std::vector<float> values{1.0f, 2.0f, 3.0f};
	const glirps::shims::Span<float> values_view = glirps::shims::toSpan(values);

	// ACT
	values_view.at(1) = 20.0f;

	// ASSERT
	EXPECT_EQ(values.at(1), 20.0f);
}
