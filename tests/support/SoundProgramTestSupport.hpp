#pragma once

#include "glirps/SoundProgram.hpp"
#include "glirps/Table.hpp"
#include "glirps/constants.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace glirps::test_support
{
	// Owns the data a test program points to.
	class TestProgramStorage
	{
	public:
		TestProgramStorage() = default;
		TestProgramStorage(const TestProgramStorage&) = delete;
		TestProgramStorage& operator=(const TestProgramStorage&) = delete;
		TestProgramStorage(TestProgramStorage&&) = default;
		TestProgramStorage& operator=(TestProgramStorage&&) = default;

		// The program under test. Every pointer member is non-owning and
		// points into one of the backing members below.
		SoundProgram program;

		// Backing storage for `program.scale`.
		std::vector<std::int32_t> scale_storage;
		// Backing storage for `program.voices`.
		std::vector<VoiceProgram> voice_storage;

		// Per-table backing storage.
		std::deque<std::vector<Segment>> segment_storage;
		std::deque<std::vector<Waveform>> waveform_value_storage;
		std::deque<std::vector<ValueRange<std::int32_t>>> int_value_storage;
		std::deque<std::vector<ValueRange<float>>> float_value_storage;
		// `bool` value storage cannot use `std::vector<bool>`: it is a bitset
		// specialization with no contiguous storage.
		std::deque<std::unique_ptr<bool[]>> bool_value_storage;
	};

	// Copies `values` into `storage` and returns a pointer to the copy, for
	// each stored value type of a `Table<Row>`.
	[[nodiscard]] inline const Waveform* storeValues(TestProgramStorage& storage, std::vector<Waveform> values)
	{
		return storage.waveform_value_storage.emplace_back(std::move(values)).data();
	}

	[[nodiscard]] inline const ValueRange<std::int32_t>* storeValues(TestProgramStorage& storage,
		std::vector<ValueRange<std::int32_t>> values)
	{
		return storage.int_value_storage.emplace_back(std::move(values)).data();
	}

	[[nodiscard]] inline const ValueRange<float>* storeValues(TestProgramStorage& storage,
		std::vector<ValueRange<float>> values)
	{
		return storage.float_value_storage.emplace_back(std::move(values)).data();
	}

	[[nodiscard]] inline const bool* storeValues(TestProgramStorage& storage, std::vector<bool> values)
	{
		auto owned_values = std::make_unique<bool[]>(values.size());
		std::ranges::copy(values, owned_values.get());
		return storage.bool_value_storage.emplace_back(std::move(owned_values)).get();
	}

	// Builds a non-owning `Table<Row>` from explicit `segments` over the
	// value pool `values`, copying both into `storage`. `loop_start_subtick`
	// set makes the table loop to it.
	template<typename Row>
	[[nodiscard]] Table<Row> makeSegmentTable(TestProgramStorage& storage,
		std::vector<Segment> segments,
		std::vector<TableValue<Row>> values,
		std::optional<std::uint32_t> loop_start_subtick = std::nullopt)
	{
		const auto segment_count = static_cast<std::uint32_t>(segments.size());
		const auto value_count = static_cast<std::uint32_t>(values.size());
		return Table<Row>{.segments = storage.segment_storage.emplace_back(std::move(segments)).data(),
			.segment_count = segment_count,
			.values = storeValues(storage, std::move(values)),
			.value_count = value_count,
			.loops = loop_start_subtick.has_value(),
			.loop_start_subtick = loop_start_subtick.value_or(0)};
	}

	// Builds the segment-table equivalent of a row table: `rows` as one
	// `step` segment holding each row for `ticks_per_value` effective ticks at
	// `tick_multiplier`, looping back to row `loop`. Looping to the last row
	// holds it.
	template<typename Row>
	[[nodiscard]] Table<Row> makeRowTable(TestProgramStorage& storage,
		std::vector<TableValue<Row>> rows,
		std::uint32_t loop = 0,
		std::uint32_t ticks_per_value = 1,
		std::uint32_t tick_multiplier = 1)
	{
		const auto interval = static_cast<std::uint16_t>(ticks_per_value * subticks_per_base_tick / tick_multiplier);
		const Segment segment{.start_subtick = 0,
			.first_value = 0,
			.value_count = static_cast<std::uint16_t>(rows.size()),
			.interval_subticks = interval,
			.interpolation_mode = InterpolationMode::step};
		return makeSegmentTable<Row>(storage, {segment}, std::move(rows), loop * static_cast<std::uint32_t>(interval));
	}

	// Builds a `Table<Waveform>` row table; see `makeRowTable`.
	[[nodiscard]] inline Table<Waveform> makeWaveformTable(TestProgramStorage& storage,
		std::vector<Waveform> rows,
		std::uint32_t loop = 0,
		std::uint32_t ticks_per_value = 1)
	{
		return makeRowTable<Waveform>(storage, std::move(rows), loop, ticks_per_value);
	}

	// Builds a `Table<bool>` row table; see `makeRowTable`.
	[[nodiscard]] inline Table<bool> makeBoolTable(TestProgramStorage& storage,
		std::vector<bool> rows,
		std::uint32_t loop = 0,
		std::uint32_t ticks_per_value = 1)
	{
		return makeRowTable<bool>(storage, std::move(rows), loop, ticks_per_value);
	}

	// Returns `rows` as fixed `ValueRange`s, each replaced by its entry in
	// `random_values` where that entry is present.
	template<typename Row>
	[[nodiscard]] std::vector<ValueRange<Row>> toValueRanges(const std::vector<Row>& rows,
		const std::vector<std::optional<ValueRange<Row>>>& random_values)
	{
		std::vector<ValueRange<Row>> values;
		values.reserve(rows.size());
		std::size_t index = 0;
		for (const Row& row : rows)
		{
			if (index < random_values.size() && random_values.at(index))
				values.push_back(*random_values.at(index));
			else
				values.push_back(ValueRange<Row>{.min = row, .max = row});
			++index;
		}
		return values;
	}

	// Builds a `Table<std::int32_t>` row table; see `makeRowTable`. A
	// present `random_values` entry makes that row a random value.
	[[nodiscard]] inline Table<std::int32_t> makeIntTable(TestProgramStorage& storage,
		const std::vector<std::int32_t>& rows,
		std::uint32_t loop = 0,
		std::uint32_t ticks_per_value = 1,
		const std::vector<std::optional<ValueRange<std::int32_t>>>& random_values = {})
	{
		return makeRowTable<std::int32_t>(storage, toValueRanges(rows, random_values), loop, ticks_per_value);
	}

	// Builds a `Table<float>` row table; see `makeRowTable`. A present
	// `random_values` entry makes that row a random value.
	[[nodiscard]] inline Table<float> makeFloatTable(TestProgramStorage& storage,
		const std::vector<float>& rows,
		std::uint32_t loop = 0,
		std::uint32_t ticks_per_value = 1,
		const std::vector<std::optional<ValueRange<float>>>& random_values = {})
	{
		return makeRowTable<float>(storage, toValueRanges(rows, random_values), loop, ticks_per_value);
	}

	// Sets `program.scale`/`scale_count` from `scale`, copying it into
	// `storage`. `program` need not be `storage.program` itself: a helper
	// that returns a `SoundProgram` by value (copied out of `storage.program`
	// before this call) passes that copy here so the copy's own pointer is
	// set, not just the original's.
	inline void setScale(TestProgramStorage& storage, SoundProgram& program, std::vector<std::int32_t> scale)
	{
		storage.scale_storage = std::move(scale);
		program.scale = storage.scale_storage.data();
		program.scale_count = static_cast<std::uint32_t>(storage.scale_storage.size());
	}

	// As the four-argument `setScale`, targeting `storage.program` itself.
	inline void setScale(TestProgramStorage& storage, std::vector<std::int32_t> scale)
	{
		setScale(storage, storage.program, std::move(scale));
	}

	// Copies `voices` into `storage` and points `program` at them. See
	// `setScale` for `program`.
	inline void setVoices(TestProgramStorage& storage, SoundProgram& program, std::vector<VoiceProgram> voices)
	{
		storage.voice_storage = std::move(voices);
		program.voices = storage.voice_storage.data();
		program.voice_count = static_cast<std::uint32_t>(storage.voice_storage.size());
	}

	// As the three-argument `setVoices`, targeting `storage.program` itself.
	inline void setVoices(TestProgramStorage& storage, std::vector<VoiceProgram> voices)
	{
		setVoices(storage, storage.program, std::move(voices));
	}

	// Points `program` at `count` default voices in `storage`; edit them
	// through `storage.voice_storage`. See `setScale` for `program`.
	inline void resizeVoices(TestProgramStorage& storage, SoundProgram& program, std::size_t count)
	{
		storage.voice_storage.assign(count, VoiceProgram{});
		program.voices = storage.voice_storage.data();
		program.voice_count = static_cast<std::uint32_t>(storage.voice_storage.size());
	}

	// As the three-argument `resizeVoices`, targeting `storage.program`
	// itself.
	inline void resizeVoices(TestProgramStorage& storage, std::size_t count)
	{
		resizeVoices(storage, storage.program, count);
	}
}
