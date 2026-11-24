#include "glirps/Array.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace
{
	constexpr glirps::shims::Array<int, 3> sample{{7, 8, 9}};

	constexpr int sumOf(const glirps::shims::Array<int, 3>& values)
	{
		int total = 0;
		for (const int value : values)
		{
			total += value;
		}
		return total;
	}

	constexpr glirps::shims::Array<int, 3> filledWith(int value)
	{
		glirps::shims::Array<int, 3> values{};
		values.fill(value);
		return values;
	}

	constexpr glirps::shims::Array<int, 3> withMiddleSet(int value)
	{
		glirps::shims::Array<int, 3> values = sample;
		values.at(1) = value;
		values.front() = values.front() + 1;
		values.back() = values.back() + 1;
		return values;
	}
}

// `Array` mirrors the `std::array` members it has. All of it is `constexpr`, so
// the checks below are evaluated at compile time.
static_assert(glirps::shims::Array<float, 5>{}.size() == 5);
static_assert(sample.elements[0] == 7 && sample.elements[1] == 8 && sample.elements[2] == 9);
// `operator[]` is the unit under test here, so the unchecked-access lint is
// disabled for this one line; every other access in this file uses `at()`.
// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
static_assert(sample[1] == 8);
static_assert(sample.at(2) == 9);
static_assert(sample.front() == 7);
static_assert(sample.back() == 9);
static_assert(sample.data() == sample.begin());
static_assert(sample.end() - sample.begin() == 3);
static_assert(sumOf(sample) == 24);
static_assert(filledWith(5).front() == 5 && filledWith(5).at(1) == 5 && filledWith(5).back() == 5);
static_assert(withMiddleSet(30).front() == 8 && withMiddleSet(30).at(1) == 30 && withMiddleSet(30).back() == 10);

// Conversion to and from `std::array` copies every element and is explicit.
static_assert(glirps::shims::Array<int, 3>::fromStdArray(std::array<int, 3>{{3, 2, 1}}).front() == 3);
static_assert(glirps::shims::Array<int, 3>::fromStdArray(std::array<int, 3>{{3, 2, 1}}).back() == 1);
static_assert(static_cast<std::array<int, 3>>(sample).front() == 7);
static_assert(static_cast<std::array<int, 3>>(sample).back() == 9);
static_assert(!std::is_convertible_v<glirps::shims::Array<int, 3>, std::array<int, 3>>);

TEST(ArrayTests, At_IndexNotLessThanSize_ThrowsOutOfRange)
{
	// ARRANGE
	glirps::shims::Array<int, 3> values{{1, 2, 3}};

	// ACT & ASSERT
	EXPECT_THROW(std::ignore = values.at(3), std::out_of_range);
}

TEST(ArrayTests, At_IndexLessThanSize_WritesThroughToElement)
{
	// ARRANGE
	glirps::shims::Array<int, 3> values{{1, 2, 3}};

	// ACT
	values.at(1) = 20;

	// ASSERT
	EXPECT_EQ(values.at(1), 20);
}
