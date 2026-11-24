#pragma once

/// @file
/// Segment tables that automate a parameter over time.

#include "constants.hpp"

#include <cstdint>
#include <type_traits>

namespace glirps
{
	/// How a segment gets from one value to the next.
	enum class InterpolationMode : std::uint8_t
	{
		/// Each value holds for one interval.
		step,
		/// Straight line between consecutive values.
		linear,
		/// Uniform Catmull-Rom spline through the values.
		catmull_rom,
	};

	/// A run of values at a regular interval, starting at one time. Independent
	/// of the row type, so every table shares it.
	struct Segment
	{
		/// Table-local time of the first value, in subticks.
		std::uint32_t start_subtick = 0;
		/// Index of the first value in `Table::values`.
		std::uint32_t first_value = 0;
		/// Number of values, at least 1.
		std::uint16_t value_count = 1;
		/// Subticks between consecutive values, at least 1.
		std::uint16_t interval_subticks = subticks_per_base_tick;
		/// How the segment moves between its values.
		InterpolationMode interpolation_mode = InterpolationMode::step;
	};

	/// A value of an arithmetic table: fixed when `min == max`, otherwise a
	/// uniform draw, made once per pass of the table.
	template<typename Row>
	struct ValueRange
	{
		/// Inclusive lower bound of the draw.
		Row min{};
		/// Upper bound of the draw. Inclusive for an integral `Row`
		/// (`std::uniform_int_distribution`); for a floating-point `Row`,
		/// `std::uniform_real_distribution` draws from the half-open `[min,
		/// max)` per the standard, so `max` itself is not guaranteed reachable.
		Row max{};
	};

	/// The stored value type of a `Table<Row>`. An arithmetic, non-`bool` row
	/// is stored as a `ValueRange<Row>`, so it can be random. `Waveform` and
	/// `bool` rows are stored as is.
	template<typename Row>
	using TableValue
		= std::conditional_t<std::is_arithmetic_v<Row> && !std::is_same_v<Row, bool>, ValueRange<Row>, Row>;

	/// A list of timed segments over one shared value pool, with an optional
	/// loop. `Row` is the row type of one parameter class. Non-owning:
	/// `segments` and `values` point into storage the caller allocates and keeps
	/// alive for at least as long as the `SoundProgram` referencing this table.
	/// The caller MAY place the segments and values of every table of a program
	/// in one contiguous block each.
	template<typename Row>
	struct Table
	{
		/// Sorted by `start_subtick`, non-overlapping. Non-owning; sized
		/// `segment_count`. SHALL be non-empty.
		const Segment* segments = nullptr;
		/// Number of entries `segments` points to.
		std::uint32_t segment_count = 0;
		/// Value pool every segment indexes into. Non-owning; sized
		/// `value_count`.
		const TableValue<Row>* values = nullptr;
		/// Number of entries `values` points to.
		std::uint32_t value_count = 0;
		/// `false` holds the last value once the last segment ends. `true`
		/// wraps to `loop_start_subtick`.
		bool loops = false;
		/// Table time evaluation wraps to on reaching the table's end.
		/// Meaningful only when `loops` is true.
		std::uint32_t loop_start_subtick = 0;
	};

	/// Authored bounds narrowing an accumulate-type table's runtime clamp.
	/// `has_min`/`has_max` false leaves the parameter unbounded (`pitch_slide`)
	/// or at its full runtime range (`pulse_width_delta`).
	struct AccumulateBounds
	{
		/// Whether `min` replaces the field's own default lower bound.
		bool has_min = false;
		/// Lower bound, meaningful only when `has_min` is true.
		float min = 0.0f;
		/// Whether `max` replaces the field's own default upper bound.
		bool has_max = false;
		/// Upper bound, meaningful only when `has_max` is true.
		float max = 0.0f;
	};
}
