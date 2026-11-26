#include "impl/Synth.hpp"

#include "glirps/frequency.hpp"
#include "impl/BlockRenderer.hpp"
#include "impl/ControlGrid.hpp"
#include "impl/DenormalGuard.hpp"
#include "impl/GritStage.hpp"
#include "impl/SumToMonoMixer.hpp"
#include "impl/VariationRng.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <random>
#include <ranges>
#include <type_traits>
#include <vector>

namespace glirps::impl
{
	namespace
	{
		constexpr float cutoff_hz_floor = 1.0f;
		// The cutoff ceiling keeps the topology-preserving filter's prewarp
		// finite; it is not an audibility limit.
		constexpr float cutoff_hz_nyquist_margin = 0.49f;

		// Range the summed semitones are clamped to.
		constexpr std::int64_t min_semitone = 0;
		constexpr std::int64_t max_semitone = 119;

		// `tick_multiplier` is bounded to `[1, max_tick_multiplier]` and each
		// `bit_depths` entry to `[min_bit_depth, max_bit_depth]`. `start`
		// checks both so a `SoundProgram` cannot pass a zero multiplier (a silently
		// never-releasing slot and a zero grid denominator) or a bit depth wide
		// enough to make the grit stage's `1 << bits` shift undefined.
		constexpr std::uint32_t max_tick_multiplier = 16;
		constexpr std::uint32_t min_bit_depth = 1;
		constexpr std::uint32_t max_bit_depth = 24;

		// Decorrelates the variation RNG's seed from the noise LFSR's, which
		// truncates `instance_seed` to its low 32 bits directly.
		constexpr std::uint64_t variation_seed_salt = 0x2545f4914f6cdd1dULL;

		// Decorrelates the seed of every random table value draw from both
		// the pitch variation RNG's (`variation_seed_salt`) and the noise
		// LFSR's.
		constexpr std::uint64_t table_random_seed_salt = 0x9e6c63d0676a9ce3ULL;

		// Cents per semitone, for adding a fractional-semitone
		// `pitch_offset` to the cents an oscillator frequency is built from.
		constexpr float cents_per_semitone = 100.0f;

		// The seeded pitch variation draws one of three outcomes, minus one
		// degree, no change, or plus one degree. `variation_step_bias` recenters the raw draw onto that range.
		constexpr std::uint64_t variation_step_count = 3;
		constexpr std::int32_t variation_step_bias = 1;

		// The per-instance seeded pitch variation offset in semitones,
		// snapped to the program's `scale` rather than applied as free
		// detuning. Draws one step from the variation RNG and snaps it to a
		// declared `scale` degree, as a delta from the degree at 0 so a
		// step of zero always reproduces the unvaried base pitch.
		// Zero when `scale` is empty: no variation is applied.
		[[nodiscard]] std::int32_t seededPitchVariationSemitones(std::span<const std::int32_t> scale,
			std::uint64_t instance_seed) noexcept
		{
			if (scale.empty())
				return 0;

			VariationRng variation_rng(instance_seed ^ variation_seed_salt);
			const std::int32_t step
				= static_cast<std::int32_t>(variation_rng.next() % variation_step_count) - variation_step_bias;

			std::size_t zero_index = 0;
			std::size_t index = 0;
			for (const std::int32_t degree : scale)
			{
				if (degree == 0)
				{
					zero_index = index;
					break;
				}
				++index;
			}

			// The index wraps around the ends of `scale`.
			const std::int64_t size = static_cast<std::int64_t>(scale.size());
			const std::int64_t raw_index = static_cast<std::int64_t>(zero_index) + step;
			const auto chosen_index = static_cast<std::size_t>(((raw_index % size) + size) % size);
			return scale.subspan(chosen_index, 1).front() - scale.subspan(zero_index, 1).front();
		}

		// A time scaled by the tick multiplier: subticks times `tick_multiplier`. The
		// block with zero-based index `n` sits at exactly `n * subticks_per_base_tick`
		// in this unit, so comparing a block time against a program time needs no
		// division and is exact for every multiplier, including one that is not a
		// power of two. 64 bits, so no instance lives long enough to overflow it.
		using ScaledTime = std::uint64_t;

		// One table's slot within its voice, or within the filter. Mixed into
		// the seed of every random value the table draws, so two tables
		// holding a random value at the same index draw independently.
		enum class TableSlot : std::uint64_t
		{
			cutoff,
			resonance,
			waveform,
			arpeggio,
			pitch_slide,
			pitch_offset,
			pulse_width_delta,
			pulse_width,
			gain,
			gate,
			count,
		};

		// Returns the seed of every random value drawn by one table of an
		// instance: the filter's tables use `voice_index` 0, and voice `v`'s
		// tables use `v + 1`.
		[[nodiscard]] std::uint64_t
		tableSeed(std::uint64_t instance_table_seed, std::uint32_t voice_index, TableSlot slot) noexcept
		{
			const std::uint64_t table_index
				= (static_cast<std::uint64_t>(voice_index) * static_cast<std::uint64_t>(TableSlot::count))
				+ static_cast<std::uint64_t>(slot);
			return VariationRng(instance_table_seed ^ table_index).next();
		}

		// Returns the seed of one random value's draw: a pure function of the
		// table, the value's index in the table's pool, and the pass of the
		// table it is drawn in. Every block of one pass therefore sees the same
		// draw, including as a Catmull-Rom neighbor, and the next pass draws
		// anew, without storage sized by the program.
		[[nodiscard]] std::uint64_t
		randomValueSeed(std::uint64_t table_seed, std::uint64_t value_index, std::uint64_t pass) noexcept
		{
			const std::uint64_t value_seed = VariationRng(table_seed ^ value_index).next();
			return VariationRng(value_seed ^ pass).next();
		}

		// Returns a segment's duration in subticks: a
		// `step` value holds for one interval, so a `step` segment lasts one
		// interval per value; an interpolating segment ends on its last value.
		[[nodiscard]] std::uint64_t segmentDuration(const Segment& segment) noexcept
		{
			const std::uint64_t value_count = segment.value_count;
			const std::uint64_t steps
				= segment.interpolation_mode == InterpolationMode::step ? value_count : value_count - 1;
			return steps * segment.interval_subticks;
		}

		// Returns a table's end in subticks: the end of its last segment.
		// `table.segments` must be non-empty, which `start` guarantees.
		template<typename Row>
		[[nodiscard]] std::uint64_t tableEnd(const Table<Row>& table) noexcept
		{
			const Segment& last = std::span<const Segment>(table.segments, table.segment_count).back();
			return last.start_subtick + segmentDuration(last);
		}

		// Where a table stands at one time: the segment in effect, the pass of
		// the table, and the scaled time since that segment's start, held at
		// the segment's end once it has ended.
		struct TablePosition
		{
			const Segment* segment = nullptr;
			std::uint64_t pass = 0;
			ScaledTime local_time = 0;
		};

		// Locates scaled table time `time` in `table`: a looping table wraps
		// any time at or after its end into `[loop_start_subtick, end)`,
		// counting passes from that loop start; `std::nullopt` is a time
		// before the first segment's start.
		template<typename Row>
		[[nodiscard]] std::optional<TablePosition>
		locate(const Table<Row>& table, ScaledTime time, std::uint32_t multiplier) noexcept
		{
			std::uint64_t pass = 0;
			const ScaledTime end = tableEnd(table) * multiplier;
			if (table.loops && time >= end)
			{
				const ScaledTime loop_start = static_cast<ScaledTime>(table.loop_start_subtick) * multiplier;
				const ScaledTime period = end - loop_start;
				pass = (time - loop_start) / period;
				time = loop_start + ((time - loop_start) % period);
			}
			// Segment starts are strictly ascending, which `start` guarantees,
			// so the segment in effect is the last one starting at or before
			// `time`.
			const std::span<const Segment> segments(table.segments, table.segment_count);
			const auto after = std::ranges::upper_bound(segments,
				time,
				{},
				[multiplier](const Segment& segment)
				{ return static_cast<ScaledTime>(segment.start_subtick) * multiplier; });
			if (after == segments.begin())
				return std::nullopt;
			const Segment& current = *std::prev(after);
			const ScaledTime local_time = time - (static_cast<ScaledTime>(current.start_subtick) * multiplier);
			return TablePosition{.segment = &current,
				.pass = pass,
				.local_time = std::min(local_time, segmentDuration(current) * multiplier)};
		}

		// Resolves one stored table value: a `Waveform` or `bool` as is, an
		// arithmetic `ValueRange` with equal bounds as its fixed value, and any
		// other `ValueRange` as a uniform draw seeded per `randomValueSeed`.
		template<typename Row>
		[[nodiscard]] Row resolveValue(const TableValue<Row>& value,
			std::uint64_t table_seed,
			std::uint64_t value_index,
			std::uint64_t pass) noexcept
		{
			if constexpr (std::is_same_v<TableValue<Row>, Row>)
			{
				return value;
			}
			else
			{
				if (value.min == value.max)
					return value.min;
				VariationRng rng(randomValueSeed(table_seed, value_index, pass));
				if constexpr (std::floating_point<Row>)
				{
					std::uniform_real_distribution<Row> distribution(value.min, value.max);
					return distribution(rng);
				}
				else
				{
					std::uniform_int_distribution<Row> distribution(value.min, value.max);
					return distribution(rng);
				}
			}
		}

		// The uniform Catmull-Rom spline through `p1` and `p2` at fraction
		// `f`, with `p0` and `p3` as the outer neighbors, in its cubic
		// Hermite form: the tangent at each end is half the difference of
		// its two neighbors.
		[[nodiscard]] float catmullRom(float p0, float p1, float p2, float p3, float f) noexcept
		{
			constexpr float half = 0.5f;
			const float start_tangent = half * (p2 - p0);
			const float end_tangent = half * (p3 - p1);
			const float rise = p2 - p1;
			const float cubic = start_tangent + end_tangent - rise - rise;
			const float quadratic = rise - start_tangent - cubic;
			return p1 + (f * (start_tangent + (f * (quadratic + (f * cubic)))));
		}

		// Evaluates `table` at scaled table time `time`.
		// `std::nullopt` before the first segment starts, on every pass,
		// where the parameter takes its base value. A segment never reads
		// another segment's values: a Catmull-Rom neighbor outside the
		// segment repeats the segment's own first or last value. Only a
		// floating-point `Row` interpolates; `start` rejects interpolation on
		// any other table. An interpolated value that is not finite, which
		// only values near the float range can produce, is also
		// `std::nullopt`, so a NaN never reaches a clamp that would pass it
		// through.
		template<typename Row>
		[[nodiscard]] std::optional<Row> evaluateTable(const Table<Row>& table,
			ScaledTime time,
			std::uint32_t multiplier,
			std::uint64_t table_seed) noexcept
		{
			const std::optional<TablePosition> position = locate(table, time, multiplier);
			if (!position)
				return std::nullopt;
			const Segment& segment = *position->segment;
			const auto values = std::span<const TableValue<Row>>(table.values, table.value_count)
									.subspan(segment.first_value, segment.value_count);
			const auto value_at = [&](std::size_t i)
			{
				return resolveValue<Row>(values.subspan(i, 1).front(),
					table_seed,
					static_cast<std::uint64_t>(segment.first_value) + i,
					position->pass);
			};
			const ScaledTime interval = static_cast<ScaledTime>(segment.interval_subticks) * multiplier;
			const std::size_t last = values.size() - 1;
			const auto value_index
				= static_cast<std::size_t>(std::min<ScaledTime>(position->local_time / interval, last));
			if constexpr (!std::floating_point<Row>)
			{
				return value_at(value_index);
			}
			else
			{
				if (segment.interpolation_mode == InterpolationMode::step || value_index == last)
					return value_at(value_index);
				const auto fraction
					= static_cast<float>(static_cast<double>(position->local_time - (value_index * interval))
						/ static_cast<double>(interval));
				const Row p1 = value_at(value_index);
				const Row p2 = value_at(value_index + 1);
				Row value{};
				if (segment.interpolation_mode == InterpolationMode::linear)
				{
					value = p1 + (fraction * (p2 - p1));
				}
				else
				{
					const Row p0 = value_at(value_index > 0 ? value_index - 1 : 0);
					const Row p3 = value_at(std::min(value_index + 2, last));
					value = catmullRom(p0, p1, p2, p3, fraction);
				}
				if (!std::isfinite(value))
					return std::nullopt;
				return value;
			}
		}

		// Per-voice table and accumulator state: `waveform`, `arpeggio`,
		// `pitch_offset`, `pulse_width` and `gain` replace the previous value,
		// `pitch_slide` and `pulse_width_delta` accumulate, and an absent
		// table leaves its parameter at the base value carried in
		// `*_current` rather than being treated as a table of zeros.
		struct VoiceTableState
		{
			// Whether the voice has become active. Set on its activation
			// block, which also fixes `table_origin`.
			bool active = false;
			// Scaled instance time of this voice's table time 0.
			ScaledTime table_origin = 0;
			Waveform waveform_current = Waveform::pulse;
			std::int32_t arpeggio_offset = 0;
			float pitch_slide_accum_cents = 0.0f;
			float pitch_offset_cents = 0.0f;
			float pulse_width_current = 0.5f;
			float gain_current = 1.0f;
			// The per-voice `gate` table's current value, ANDed with the
			// chip-wide/one-shot-derived gate at the `evaluateTick` call
			// site. Defaults true: an absent `gate` table behaves as
			// always-true, leaving the chip-wide gate as the sole
			// determinant.
			bool gate_on = true;
		};

		// Per-chip filter table state.
		struct FilterTableState
		{
			float cutoff_current = 20000.0f;
			float resonance_current = 0.707f;
		};

		// Returns true when `table` is malformed, which `start` rejects with
		// `StartError::invalid_program`: no segment or no value pool; a
		// segment with no value, a zero interval, or values outside the pool;
		// segment starts not strictly ascending or a segment starting before
		// the previous one ends; a `step` segment of several values shorter
		// than one effective tick each, which could never be heard; an
		// interpolation mode `allows_interpolation` forbids or out of range; a
		// loop start at or past the end; or a `ValueRange` that is non-finite
		// or inverted. The evaluator relies on every one of these to index and
		// divide safely.
		template<typename Row>
		[[nodiscard]] bool
		isInvalidTable(const Table<Row>& table, bool allows_interpolation, std::uint32_t multiplier) noexcept
		{
			if (table.segments == nullptr || table.segment_count == 0 || table.values == nullptr
				|| table.value_count == 0)
				return true;
			bool first = true;
			std::uint64_t previous_start = 0;
			std::uint64_t previous_end = 0;
			for (const Segment& segment : std::span<const Segment>(table.segments, table.segment_count))
			{
				if (segment.value_count == 0 || segment.interval_subticks == 0)
					return true;
				if (static_cast<std::uint64_t>(segment.first_value) + segment.value_count > table.value_count)
					return true;
				if (!first && (segment.start_subtick <= previous_start || segment.start_subtick < previous_end))
					return true;
				switch (segment.interpolation_mode)
				{
				case InterpolationMode::step:
					if (segment.value_count > 1
						&& static_cast<std::uint64_t>(segment.interval_subticks) * multiplier < subticks_per_base_tick)
						return true;
					break;
				case InterpolationMode::linear:
				case InterpolationMode::catmull_rom:
					if (!allows_interpolation)
						return true;
					break;
				default:
					return true;
				}
				first = false;
				previous_start = segment.start_subtick;
				previous_end = segment.start_subtick + segmentDuration(segment);
			}
			if (table.loops && table.loop_start_subtick >= previous_end)
				return true;
			if constexpr (!std::is_same_v<TableValue<Row>, Row>)
			{
				for (const ValueRange<Row>& value : std::span<const ValueRange<Row>>(table.values, table.value_count))
				{
					// A range wider than the float range makes
					// `std::uniform_real_distribution` undefined, so the width
					// itself must be finite too.
					if constexpr (std::floating_point<Row>)
					{
						if (!std::isfinite(value.min) || !std::isfinite(value.max)
							|| !std::isfinite(value.max - value.min))
							return true;
					}
					if (value.max < value.min)
						return true;
				}
			}
			return false;
		}

		// Returns true when any present table of `program` is malformed per
		// `isInvalidTable`, or a voice holds both a `pulse_width` and a
		// `pulse_width_delta` table, which `start` rejects with
		// `StartError::invalid_program`. Only `gain`, `pitch_offset`,
		// `pulse_width`, `cutoff` and `resonance` may interpolate: `waveform`
		// and `gate` values are discrete, `arpeggio` is whole semitones, and
		// an accumulate table adds its value once per effective tick, so an
		// interpolated step would change the accumulated sum rather than
		// smooth it. Assumes a valid `tick_multiplier`, which `start` checks
		// first.
		[[nodiscard]] bool hasInvalidTable(const SoundProgram& program) noexcept
		{
			const std::uint32_t multiplier = program.grit.tick_multiplier;
			const FilterTables& filter = program.filter.tables;
			if (filter.has_cutoff && isInvalidTable(filter.cutoff, true, multiplier))
				return true;
			if (filter.has_resonance && isInvalidTable(filter.resonance, true, multiplier))
				return true;
			for (const VoiceProgram& voice : std::span<const VoiceProgram>(program.voices, program.voice_count))
			{
				const VoiceTables& tables = voice.tables;
				if (tables.has_pulse_width && tables.has_pulse_width_delta)
					return true;
				if ((tables.has_waveform && isInvalidTable(tables.waveform, false, multiplier))
					|| (tables.has_arpeggio && isInvalidTable(tables.arpeggio, false, multiplier))
					|| (tables.has_pitch_slide && isInvalidTable(tables.pitch_slide, false, multiplier))
					|| (tables.has_pitch_offset && isInvalidTable(tables.pitch_offset, true, multiplier))
					|| (tables.has_pulse_width_delta && isInvalidTable(tables.pulse_width_delta, false, multiplier))
					|| (tables.has_pulse_width && isInvalidTable(tables.pulse_width, true, multiplier))
					|| (tables.has_gain && isInvalidTable(tables.gain, true, multiplier))
					|| (tables.has_gate && isInvalidTable(tables.gate, false, multiplier)))
					return true;
			}
			return false;
		}

		// Returns true when `program`'s `voices` list is empty or longer than
		// `max_voices_per_instance`, which `start` rejects with
		// `StartError::invalid_program`, since the count would exceed the
		// per-instance voice slots or leave nothing to render.
		[[nodiscard]] bool hasInvalidVoiceCount(const SoundProgram& program) noexcept
		{
			return program.voice_count == 0 || program.voice_count > max_voices_per_instance;
		}

		// Returns true when a voice's `pitch_slide_bounds`/`pulse_width_delta_bounds`
		// has a non-finite bound, a bound outside the field's own default
		// floor/ceiling, or `max` below `min` when both are present, which
		// `start` rejects with `StartError::invalid_program`. Without this
		// check `clampAccumulate`'s
		// `std::clamp` could see an inverted `[lo, hi]` interval, which is
		// undefined behavior.
		[[nodiscard]] bool hasInvalidAccumulateBounds(const SoundProgram& program) noexcept
		{
			const auto invalid = [](const AccumulateBounds& bounds, float default_floor, float default_ceiling)
			{
				if (bounds.has_min
					&& (!std::isfinite(bounds.min) || bounds.min < default_floor || bounds.min > default_ceiling))
					return true;
				if (bounds.has_max
					&& (!std::isfinite(bounds.max) || bounds.max < default_floor || bounds.max > default_ceiling))
					return true;
				return bounds.has_min && bounds.has_max && bounds.max < bounds.min;
			};
			for (const VoiceProgram& voice : std::span<const VoiceProgram>(program.voices, program.voice_count))
			{
				if (invalid(voice.tables.pitch_slide_bounds,
						-std::numeric_limits<float>::infinity(),
						std::numeric_limits<float>::infinity()))
					return true;
				if (invalid(voice.tables.pulse_width_delta_bounds, pulse_width_floor, pulse_width_ceiling))
					return true;
			}
			return false;
		}

		// Returns true when a voice's `ring_mod_source`/`hard_sync_source`,
		// when present, names a voice index outside `[0, voices.size() - 1]`
		// or its own voice's index, which `start` rejects with
		// `StartError::invalid_program`. Without this check every render would
		// read a voice that does not exist.
		[[nodiscard]] bool hasInvalidRoutingSource(const SoundProgram& program) noexcept
		{
			std::uint32_t i = 0;
			for (const VoiceProgram& voice : std::span<const VoiceProgram>(program.voices, program.voice_count))
			{
				if (voice.has_ring_mod_source
					&& (voice.ring_mod_source >= program.voice_count || voice.ring_mod_source == i))
					return true;
				if (voice.has_hard_sync_source
					&& (voice.hard_sync_source >= program.voice_count || voice.hard_sync_source == i))
					return true;
				++i;
			}
			return false;
		}

		// Returns true when `program`'s grit profile declares a
		// `tick_multiplier` outside `[1, max_tick_multiplier]`, which `start`
		// rejects with `StartError::invalid_program`.
		[[nodiscard]] bool hasOutOfRangeTickMultiplier(const SoundProgram& program) noexcept
		{
			return program.grit.tick_multiplier < 1 || program.grit.tick_multiplier > max_tick_multiplier;
		}

		// Returns true when any of `program`'s grit profile bit depths falls
		// outside `[min_bit_depth, max_bit_depth]`, which `start` rejects
		// with `StartError::invalid_program`.
		[[nodiscard]] bool hasOutOfRangeBitDepths(const SoundProgram& program) noexcept
		{
			const BitDepths& depths = program.grit.bit_depths;
			const auto in_range = [](std::uint32_t depth) { return depth >= min_bit_depth && depth <= max_bit_depth; };
			return !in_range(depths.pitch) || !in_range(depths.pulse_width) || !in_range(depths.cutoff)
				|| !in_range(depths.resonance) || !in_range(depths.gain);
		}

		// Writes `block`'s filter and chip-gain fields from the shared
		// filter's tables at scaled instance time `time`, clamped to their
		// stated ranges. The filter's tables use instance time directly: the
		// filter is shared, so it has no per-voice `start_subtick` to offset by.
		void advanceFilterAndChipGain(ParameterBlock& block,
			FilterTableState& filter_state,
			const FilterProgram& filter,
			float output_gain,
			float cutoff_ceiling,
			ScaledTime time,
			std::uint32_t multiplier,
			std::uint64_t instance_table_seed) noexcept
		{
			// Before a table's first segment, on any pass, its parameter
			// takes the base value, as for an absent table.
			if (filter.tables.has_cutoff)
			{
				filter_state.cutoff_current = evaluateTable(filter.tables.cutoff,
					time,
					multiplier,
					tableSeed(instance_table_seed, 0, TableSlot::cutoff))
												  .value_or(filter.cutoff_hz);
			}
			if (filter.tables.has_resonance)
			{
				filter_state.resonance_current = evaluateTable(filter.tables.resonance,
					time,
					multiplier,
					tableSeed(instance_table_seed, 0, TableSlot::resonance))
													 .value_or(filter.resonance_q);
			}
			block.filter_modes = filter.modes;
			block.cutoff_hz = std::clamp(filter_state.cutoff_current, cutoff_hz_floor, cutoff_ceiling);
			block.resonance_q = std::clamp(filter_state.resonance_current, resonance_q_floor, resonance_q_ceiling);
			block.output_gain = std::clamp(output_gain, gain_floor, gain_ceiling);
		}

		// Clamps `value` to `bounds`, falling back to `default_floor`/
		// `default_ceiling` on whichever side is absent, so a bound with only
		// `min` or only `max` clamps that side alone.
		[[nodiscard]] float clampAccumulate(float value,
			const AccumulateBounds& bounds,
			float default_floor,
			float default_ceiling) noexcept
		{
			return std::clamp(value,
				bounds.has_min ? bounds.min : default_floor,
				bounds.has_max ? bounds.max : default_ceiling);
		}

		// Where and how one voice's tables are evaluated for one block: its
		// scaled table time, the multiplier the time is scaled by, and the
		// instance's table seed and the voice's index for random values.
		struct VoiceTableClock
		{
			ScaledTime time = 0;
			std::uint32_t multiplier = 1;
			std::uint64_t instance_table_seed = 0;
			std::uint32_t voice_index = 0;
		};

		// Evaluates one voice's tables for one effective tick: an absent table
		// leaves its parameter at its base value, and a table before its first
		// segment, on any pass, sets it to that base value; an accumulate table
		// adds nothing there. `pitch_slide` and
		// `pulse_width_delta` add their value once per effective tick, so their
		// effective rate scales with `tick_multiplier`, and are further clamped
		// by their authored `*_bounds`; `pulse_width_delta`'s `[0, 1]` clamp
		// is `clampAccumulate`'s default when no bound narrows it.
		void
		advanceVoiceTables(VoiceTableState& state, const VoiceProgram& voice, const VoiceTableClock& clock) noexcept
		{
			const VoiceTables& tables = voice.tables;
			const auto evaluate = [&clock](const auto& table, TableSlot slot)
			{
				// Voice `v`'s tables seed as voice `v + 1`; the filter's use 0.
				return evaluateTable(table,
					clock.time,
					clock.multiplier,
					tableSeed(clock.instance_table_seed, clock.voice_index + 1, slot));
			};
			if (tables.has_waveform)
				state.waveform_current = evaluate(tables.waveform, TableSlot::waveform).value_or(voice.waveform);
			if (tables.has_arpeggio)
				state.arpeggio_offset = evaluate(tables.arpeggio, TableSlot::arpeggio).value_or(0);
			if (tables.has_pitch_slide)
			{
				if (const auto value = evaluate(tables.pitch_slide, TableSlot::pitch_slide))
				{
					state.pitch_slide_accum_cents = clampAccumulate(state.pitch_slide_accum_cents + *value,
						tables.pitch_slide_bounds,
						-std::numeric_limits<float>::infinity(),
						std::numeric_limits<float>::infinity());
				}
			}
			if (tables.has_pitch_offset)
				state.pitch_offset_cents
					= evaluate(tables.pitch_offset, TableSlot::pitch_offset).value_or(0.0f) * cents_per_semitone;
			if (tables.has_pulse_width_delta)
			{
				if (const auto value = evaluate(tables.pulse_width_delta, TableSlot::pulse_width_delta))
				{
					state.pulse_width_current = clampAccumulate(state.pulse_width_current + *value,
						tables.pulse_width_delta_bounds,
						pulse_width_floor,
						pulse_width_ceiling);
				}
			}
			if (tables.has_pulse_width)
				state.pulse_width_current
					= evaluate(tables.pulse_width, TableSlot::pulse_width).value_or(voice.pulse_width);
			if (tables.has_gain)
				state.gain_current = evaluate(tables.gain, TableSlot::gain).value_or(voice.gain);
			if (tables.has_gate)
				state.gate_on = evaluate(tables.gate, TableSlot::gate).value_or(true);
		}

		// Resets `state` to the fresh state a newly-active voice has: every
		// accumulator at its initial value, current values at `voice`'s base
		// parameters, and the gate-table state at its absent-table default
		// (always-true). Used both by `start` for every voice and by
		// `evaluateTick` for a voice deferred by `start_subtick` reaching its
		// activation.
		void resetVoiceTableState(VoiceTableState& state, const VoiceProgram& voice) noexcept
		{
			state = VoiceTableState{};
			state.waveform_current = voice.waveform;
			state.pulse_width_current = voice.pulse_width;
			state.gain_current = voice.gain;
		}

		// Scaled instance time at which an instance onset delay of
		// `onset_delay_base_ticks` has elapsed. The delay
		// stays in base ticks, so onset stays on the base grid, and is
		// 1-indexed: a delay of 0 and a delay of 1 both activate on the first
		// block `evaluateTick` emits, and the only difference between them is
		// where the gate opens: `start`'s base block for 0, the first emitted
		// (grid-positioned) block for 1. A delay of `N` activates on the first
		// block of the Nth base tick.
		[[nodiscard]] constexpr ScaledTime onsetActivationTime(std::uint32_t onset_delay_base_ticks,
			std::uint32_t tick_multiplier) noexcept
		{
			if (onset_delay_base_ticks == 0)
				return 0;
			const ScaledTime blocks_before_onset
				= static_cast<ScaledTime>(onset_delay_base_ticks - 1) * tick_multiplier;
			return blocks_before_onset * subticks_per_base_tick;
		}

		// Assembles one voice's oscillator frequency in hertz from the base
		// pitch, the seeded per-instance variation, the arpeggio offset, and
		// `offset_cents`: the accumulated pitch slide, the `pitch_offset`
		// table, and any external pitch. The arpeggio offset is in semitones
		// from the base pitch, and the variation is already snapped to a
		// `scale` degree. Summed in 64 bits so a huge `arpeggio` cannot
		// overflow. The result is clamped to Nyquist by the caller.
		[[nodiscard]] float assembleVoiceFrequency(std::int32_t base_semitone,
			std::int32_t pitch_variation_semitones,
			std::int32_t arpeggio_offset,
			std::int32_t base_cents,
			float offset_cents) noexcept
		{
			const std::int64_t total_semitones
				= static_cast<std::int64_t>(base_semitone) + pitch_variation_semitones + arpeggio_offset;
			return frequencyHertz(
				static_cast<std::int32_t>(std::clamp<std::int64_t>(total_semitones, min_semitone, max_semitone)),
				static_cast<float>(base_cents) + offset_cents);
		}

		// Evaluates one block's gate state: a one-shot program
		// (`has_release_subtick` true) holds the gate on for every block whose
		// time is below `release_subtick`, compared exactly in scaled time; a
		// sustained program takes the consumer's held gate directly.
		[[nodiscard]] bool
		computeGateOn(const GateProgram& gate, ScaledTime block_time, std::uint32_t multiplier, bool gate_held) noexcept
		{
			if (!gate.has_release_subtick)
				return gate_held;
			return block_time < static_cast<ScaledTime>(gate.release_subtick) * multiplier;
		}

		// Writes one voice's evaluated parameters into `voice_block`,
		// clamping frequency and pulse width to their runtime ranges.
		void writeVoiceBlock(VoiceParameters& voice_block,
			const VoiceTableState& state,
			float frequency,
			float nyquist,
			bool gate_on) noexcept
		{
			voice_block.waveform = state.waveform_current;
			voice_block.frequency_hz = std::clamp(frequency, frequency_hz_floor, nyquist);
			voice_block.pulse_width = std::clamp(state.pulse_width_current, pulse_width_floor, pulse_width_ceiling);
			voice_block.gain = std::clamp(state.gain_current, gain_floor, gain_ceiling);
			voice_block.gate_on = gate_on;
		}

		// Applied after the grit stage.
		void clampParameterBlock(ParameterBlock& block, float nyquist, float cutoff_ceiling) noexcept
		{
			block.cutoff_hz = std::clamp(block.cutoff_hz, cutoff_hz_floor, cutoff_ceiling);
			block.resonance_q = std::clamp(block.resonance_q, resonance_q_floor, resonance_q_ceiling);
			block.output_gain = std::clamp(block.output_gain, gain_floor, gain_ceiling);
			for (VoiceParameters& voice_block : std::span<VoiceParameters>(block.voices.data(), block.voice_count))
			{
				voice_block.frequency_hz = std::clamp(voice_block.frequency_hz, frequency_hz_floor, nyquist);
				voice_block.pulse_width = std::clamp(voice_block.pulse_width, pulse_width_floor, pulse_width_ceiling);
				voice_block.gain = std::clamp(voice_block.gain, gain_floor, gain_ceiling);
			}
		}
	}

	struct Synth::Impl
	{
		// Control-side-only per-slot state. `render` never reads or writes any
		// of it.
		// Split from render-side state and the shared `SlotState`.
		struct ControlSlotState
		{
			// Non-owning; see `Synth::start`.
			const SoundProgram* program = nullptr;
			std::uint32_t reserved_voices = 0;
			// Blocks emitted so far; sets table timing and one-shot release.
			std::uint64_t blocks_emitted = 0;
			// Base ticks this instance's onset is withheld for so it
			// lands on the base control grid, set by `start` and converted into
			// scaled time by `onsetActivationTime`. Zero for an instance that
			// gated on at `start`.
			std::uint32_t onset_delay_base_ticks = 0;
			// Consumer-held gate state for a sustained program
			// (`gate.has_release_subtick` false), set through `Synth::setGate`.
			// Ignored by a one-shot program.
			bool gate_held = true;
			// Seeded per-instance pitch variation, fixed for the instance's
			// whole life.
			std::int32_t pitch_variation_semitones = 0;
			// Table and accumulator state, owned by the evaluator
			// rather than the chip instance, which owns no table state.
			std::array<VoiceTableState, max_voices_per_instance> voice_table_state;
			FilterTableState filter_table_state;
			// Seed of every random table value this instance draws, derived
			// from `instance_seed` by `start`, so a given seed always reproduces
			// the same sound. Each draw
			// is a pure function of this seed, the table, the value and the
			// pass, so no generator state persists between blocks.
			std::uint64_t table_seed = 0;
			// Authored chip gain times the latest `gain_multiplier`,
			// unclamped. `evaluateTick` clamps its blocks; `start`'s base
			// block is not clamped.
			float chip_output_gain = 1.0f;
			// External per-tick pitch offset in cents, set through `start`
			// or `Synth::updateControlParams` and added to the authored base
			// pitch (base cents plus the accumulated pitch slide) by
			// `evaluateTick`. Zero leaves the authored pitch unchanged.
			float external_pitch_cents = 0.0f;
			// External per-tick filter cutoff override in hertz, set
			// through `start` or `Synth::updateControlParams`. Meaningful
			// only when `external_cutoff_active` is true.
			float external_cutoff_hz = 0.0f;
			// Whether `external_cutoff_hz` replaces the authored cutoff
			// value `evaluateTick` derives. A flagged `cutoff_hz` sets this
			// true, including a flagged zero: the field's "not overridden"
			// state is this flag, not a sentinel on `external_cutoff_hz`.
			bool external_cutoff_active = false;
		};

		// Render-side state. The control side writes it only in construction
		// and `configure`, with no `render` running, and in `start`, after
		// `render` has retired the slot and before publishing `generation`.
		struct RenderSlotState
		{
			UniquePtr<BlockRenderer> renderer;
			ParameterBlock current_block;
			std::array<float, sub_block_frame_count> staging{};
		};

		// Atomics shared by the control and render sides. `start` reuses a
		// freed slot only once `retired_generation` equals `generation`,
		// proving `render` has let go of it. `configure` also writes
		// `retired_generation`, safe only because `render` is stopped.
		struct SlotState
		{
			// Written by the control side. Odd = claimed, even = free; 0 is
			// never issued, as it marks a null handle.
			std::atomic<std::uint32_t> generation = 0;

			// Written by `render` and `configure`: the free generation last
			// acknowledged.
			std::atomic<std::uint32_t> retired_generation = 0;

			// Written by `render`: the generation that completed. Stays set
			// even if the gate reopens.
			std::atomic<std::uint32_t> completed_generation = 0;

			// Written by `start` and `evaluateTick`, read by `render`: the
			// chip-wide gate before per-voice `gate` tables apply. `render`
			// latches completion only while it is false, since a `gate` table
			// can silence every envelope between retriggers.
			std::atomic<bool> chip_gate_open = true;
			// Written by `start` and `evaluateTick`, read by `render`: whether
			// any emitted block gates a voice on. If none has, `render` does
			// not wait for `gate_opened`.
			std::atomic<bool> gate_emitted = false;
			// Written by `start` and `applyBlock`, read by `render`: whether a
			// gated-on block has reached the renderer. If a gate-on was
			// emitted, `render` waits for this, so a short one-shot is not
			// reclaimed before it sounds.
			std::atomic<bool> gate_opened = false;
		};

		std::uint32_t sample_rate_hz = 0;
		float grit_scale = 1.0f;
		Mixer* mixer = nullptr;
		SumToMonoMixer default_mixer;
		std::vector<ControlSlotState, Allocator<ControlSlotState>> control_slots;
		std::vector<RenderSlotState, Allocator<RenderSlotState>> render_slots;
		std::vector<SlotState, Allocator<SlotState>> slot_states;
		std::uint32_t available_voices = 0;

		explicit Impl(std::uint32_t max_chip_instances, std::uint32_t max_voices, const AllocatorRef& allocator)
			: control_slots(max_chip_instances, Allocator<ControlSlotState>(allocator))
			, render_slots(max_chip_instances, Allocator<RenderSlotState>(allocator))
			, slot_states(max_chip_instances, Allocator<SlotState>(allocator))
			, available_voices(max_voices)
		{
			for (RenderSlotState& slot : render_slots)
				slot.renderer = makeUnique<BlockRenderer>(allocator,
					default_render_sample_rate_hz,
					default_oversampling_factor,
					allocator);
		}

		// Returns true when `instance` names a live slot: in range and
		// matching generation. Equality alone implies claimed, since only
		// odd values are ever minted as a handle's generation. Control
		// side.
		[[nodiscard]] bool isLive(ChipInstanceHandle instance) const noexcept
		{
			if (instance.isNull())
				return false;
			if (instance.getSlot().get() >= slot_states.size())
				return false;
			const SlotState& state = slot_states.at(instance.getSlot().get());
			return state.generation.load() == instance.getGeneration();
		}
	};

	Synth::Synth(std::uint32_t max_chip_instances, std::uint32_t max_voices, const AllocatorRef& allocator)
		: impl_(makeUnique<Impl>(allocator, max_chip_instances, max_voices, allocator))
	{
	}

	Synth::~Synth() = default;

	void Synth::configure(std::uint32_t sample_rate_hz) noexcept
	{
		configure(sample_rate_hz, default_oversampling_factor);
	}

	void Synth::configure(std::uint32_t sample_rate_hz, std::uint32_t oversampling_factor) noexcept
	{
		// Discards every instance. Writing render-owned state here is safe
		// only because the consumer stops `render` first.
		for (std::size_t s = 0; s < impl_->control_slots.size(); ++s)
		{
			Impl::ControlSlotState& control = impl_->control_slots.at(s);
			Impl::SlotState& state = impl_->slot_states.at(s);
			if (state.generation.load() % 2 == 1)
			{
				state.generation.fetch_add(1);
				control.program = nullptr;
				control.voice_table_state = {};
				control.filter_table_state = {};
				impl_->available_voices += control.reserved_voices;
				control.reserved_voices = 0;
			}
			state.retired_generation.store(state.generation.load());
			impl_->render_slots.at(s).renderer->reconfigure(sample_rate_hz, oversampling_factor);
		}
		impl_->sample_rate_hz = sample_rate_hz;
	}

	void Synth::setGritScale(float scale) noexcept
	{
		// `std::clamp` passes NaN through, so a non-finite scale becomes 0.
		impl_->grit_scale = std::isfinite(scale) ? std::clamp(scale, 0.0f, 1.0f) : 0.0f;
	}

	void Synth::setMixer(Mixer& mixer) noexcept { impl_->mixer = &mixer; }

	std::expected<ChipInstanceHandle, StartError> Synth::start(const SoundProgram& program,
		std::uint64_t instance_seed,
		std::uint32_t onset_delay_base_ticks,
		ControlParams params) noexcept
	{
		// `hasInvalidTable` checks the effective-tick length of a `step` value
		// against the multiplier, so the multiplier is checked before it.
		if (hasInvalidVoiceCount(program) || hasOutOfRangeTickMultiplier(program) || hasInvalidTable(program)
			|| hasOutOfRangeBitDepths(program) || hasInvalidAccumulateBounds(program)
			|| hasInvalidRoutingSource(program))
			return std::unexpected(StartError::invalid_program);

		// Slots are checked before voices, so `chip_instances_exhausted` wins
		// when both are short.
		std::size_t free_index = impl_->control_slots.size();
		for (std::size_t s = 0; s < impl_->slot_states.size(); ++s)
		{
			const Impl::SlotState& candidate = impl_->slot_states.at(s);
			const std::uint32_t candidate_generation = candidate.generation.load();
			// Free requires generation to be even (no live claim) AND
			// render to have itself acknowledged that release: without the
			// second check, `render` might still be mid-flight on this
			// slot's previous instance.
			if (candidate_generation % 2 == 0 && candidate.retired_generation.load() == candidate_generation)
			{
				free_index = s;
				break;
			}
		}
		if (free_index == impl_->control_slots.size())
			return std::unexpected(StartError::chip_instances_exhausted);

		if (program.voice_count > impl_->available_voices)
			return std::unexpected(StartError::voices_exhausted);

		Impl::ControlSlotState& control = impl_->control_slots.at(free_index);
		Impl::RenderSlotState& render_slot = impl_->render_slots.at(free_index);
		Impl::SlotState& state = impl_->slot_states.at(free_index);

		control.program = &program;
		control.reserved_voices = program.voice_count;
		impl_->available_voices -= control.reserved_voices;
		control.blocks_emitted = 0;
		control.onset_delay_base_ticks = onset_delay_base_ticks;
		control.gate_held = true;
		state.chip_gate_open.store(true);
		state.gate_emitted.store(false);
		state.gate_opened.store(false);
		control.pitch_variation_semitones
			= seededPitchVariationSemitones(std::span<const std::int32_t>(program.scale, program.scale_count),
				instance_seed);
		// Every random table value draw of this instance's whole life is
		// seeded from this, so a given seed always reproduces the same sound.
		control.table_seed = instance_seed ^ table_random_seed_salt;
		// Reset for a reused slot: a stale external control from the previous
		// instance would otherwise shift this fresh instance's pitch or
		// cutoff until `updateControlParams` runs again. `params`'s flagged
		// and finite fields are then applied below, before the base block is
		// written, so the first evaluated block already carries them.
		control.external_pitch_cents = 0.0f;
		control.external_cutoff_hz = 0.0f;
		control.external_cutoff_active = false;
		if ((params.flags & ControlFlag::pitch_cents) != 0 && std::isfinite(params.pitch_cents))
			control.external_pitch_cents = params.pitch_cents;
		if ((params.flags & ControlFlag::cutoff_hz) != 0 && std::isfinite(params.cutoff_hz))
		{
			control.external_cutoff_hz = params.cutoff_hz;
			control.external_cutoff_active = true;
		}

		// Resets for slot reuse.
		render_slot.renderer->resetState();

		const float base_frequency = frequencyHertz(program.base_pitch.semitone + control.pitch_variation_semitones,
			static_cast<float>(program.base_pitch.cents));
		ParameterBlock base_block;
		base_block.filter_modes = program.filter.modes;
		base_block.cutoff_hz = program.filter.cutoff_hz;
		base_block.resonance_q = program.filter.resonance_q;
		control.chip_output_gain = program.output_gain;
		if ((params.flags & ControlFlag::gain_multiplier) != 0 && std::isfinite(params.gain_multiplier))
			control.chip_output_gain = program.output_gain * params.gain_multiplier;
		base_block.output_gain = control.chip_output_gain;
		base_block.voice_count = program.voice_count;
		const std::span<VoiceParameters> base_voices(base_block.voices.data(), program.voice_count);
		std::uint32_t i = 0;
		for (const VoiceProgram& voice : std::span<const VoiceProgram>(program.voices, program.voice_count))
		{
			VoiceParameters& base_voice = base_voices.subspan(i, 1).front();
			base_voice.waveform = voice.waveform;
			base_voice.frequency_hz = base_frequency;
			base_voice.pulse_width = voice.pulse_width;
			base_voice.gain = voice.gain;
			// Every voice gates on at start and renders from its program base
			// values until a block replaces them, except a voice whose
			// `start_subtick` is nonzero, which instead holds inactive (gated
			// off) until `evaluateTick` reaches that time, and except an
			// instance started with a nonzero onset delay, whose every voice
			// holds inactive until `evaluateTick` reaches the delayed tick, so
			// onset lands on the base control grid.
			base_voice.gate_on = voice.start_subtick == 0 && onset_delay_base_ticks == 0;
			render_slot.renderer->setVoiceEnvelopeSeconds(i,
				voice.envelope.attack_seconds,
				voice.envelope.decay_seconds,
				voice.envelope.release_seconds,
				voice.envelope.sustain_level);
			// Every LFSR is seeded per chip instance, deterministically: every
			// voice of this instance shares `instance_seed`.
			render_slot.renderer->seedVoiceNoise(i, static_cast<std::uint32_t>(instance_seed));

			// Applies ring-mod/hard-sync routing before the
			// instance's first `evaluateTick`, so every render of the instance,
			// including those from its base values, is routed.
			// `resetState` above already cleared any previous instance's
			// routing, so an absent source here correctly stays unrouted.
			if (voice.has_ring_mod_source)
				render_slot.renderer->setRingModSource(i, voice.ring_mod_source);
			if (voice.has_hard_sync_source)
				render_slot.renderer->setHardSyncSource(i, voice.hard_sync_source);

			// Seed the table evaluator's per-voice state from the program's
			// base values, so an absent table leaves its parameter at that
			// value for the whole sound rather than acting as a table of zeros.
			resetVoiceTableState(control.voice_table_state.at(i), voice);
			++i;
		}
		control.filter_table_state = FilterTableState{};
		control.filter_table_state.cutoff_current = program.filter.cutoff_hz;
		control.filter_table_state.resonance_current = program.filter.resonance_q;

		render_slot.current_block = base_block;
		render_slot.renderer->applyBaseBlock(base_block);
		render_slot.renderer->setFilterSaturation(program.filter.saturation);

		// A base block that gates a voice on both emits and delivers that
		// gate in one step, since `applyBaseBlock` above is the delivery.
		const bool base_block_gates_on
			= std::ranges::any_of(std::span<const VoiceParameters>(base_block.voices.data(), base_block.voice_count),
				[](const VoiceParameters& voice) { return voice.gate_on; });
		state.gate_emitted.store(base_block_gates_on);
		state.gate_opened.store(base_block_gates_on);

		// Everything render-visible for this slot is written above this
		// line; the `generation` publish below is what makes it visible to
		// `render`, which loads it before touching any slot field.
		// Publish LAST: the returned handle uses the POST-increment (odd) value.
		const std::uint32_t new_generation = state.generation.fetch_add(1) + 1;
		return ChipInstanceHandle(static_cast<std::uint32_t>(free_index), new_generation);
	}

	bool Synth::isComplete(ChipInstanceHandle instance) const noexcept
	{
		if (!impl_->isLive(instance))
			return true;
		return impl_->slot_states.at(instance.getSlot().get()).completed_generation.load() == instance.getGeneration();
	}

	void Synth::release(ChipInstanceHandle instance) noexcept
	{
		if (!impl_->isLive(instance))
			return;
		Impl::ControlSlotState& control = impl_->control_slots.at(instance.getSlot().get());
		Impl::SlotState& state = impl_->slot_states.at(instance.getSlot().get());

		state.generation.fetch_add(1);  // invalidate the handle (now even)

		control.program = nullptr;  // no reference counting; just clears the pointer
		control.voice_table_state = {};
		control.filter_table_state = {};
		impl_->available_voices += control.reserved_voices;
		control.reserved_voices = 0;

		// Deliberately does NOT touch render_slots[instance.getSlot().get()] and does
		// NOT touch retired_generation. The slot becomes reusable only once
		// render itself observes and publishes retired_generation ==
		// generation, or a subsequent configure unconditionally resyncs
		// it, so `start` never reclaims a slot `render` may still be
		// mid-flight on.
	}

	void Synth::setGate(ChipInstanceHandle instance, bool gate_on) noexcept
	{
		if (!impl_->isLive(instance))
			return;
		Impl::ControlSlotState& control = impl_->control_slots.at(instance.getSlot().get());
		// No effect on a one-shot instance, whose gate is driven by
		// the block time against `gate.release_subtick` instead.
		if (control.program->gate.has_release_subtick)
			return;
		control.gate_held = gate_on;
	}

	void Synth::updateControlParams(ChipInstanceHandle instance, ControlParams params) noexcept
	{
		if (!impl_->isLive(instance))
			return;
		Impl::ControlSlotState& control = impl_->control_slots.at(instance.getSlot().get());
		// Non-finite values are ignored because the downstream `std::clamp`
		// calls pass NaN through.
		if ((params.reset_flags & ControlFlag::pitch_cents) != 0)
			control.external_pitch_cents = 0.0f;
		else if ((params.flags & ControlFlag::pitch_cents) != 0 && std::isfinite(params.pitch_cents))
			control.external_pitch_cents = params.pitch_cents;
		if ((params.reset_flags & ControlFlag::cutoff_hz) != 0)
		{
			control.external_cutoff_hz = 0.0f;
			control.external_cutoff_active = false;
		}
		else if ((params.flags & ControlFlag::cutoff_hz) != 0 && std::isfinite(params.cutoff_hz))
		{
			control.external_cutoff_hz = params.cutoff_hz;
			control.external_cutoff_active = true;
		}
		// The gain multiplier folds into the chip output gain at both entry
		// points, rather than compounding on top of the
		// previous value: the authored gain is re-scaled fresh from this
		// call's multiplier. A reset re-reads the authored gain directly,
		// since no separate "current multiplier" is retained to invert.
		if ((params.reset_flags & ControlFlag::gain_multiplier) != 0)
			control.chip_output_gain = control.program->output_gain;
		else if ((params.flags & ControlFlag::gain_multiplier) != 0 && std::isfinite(params.gain_multiplier))
			control.chip_output_gain = control.program->output_gain * params.gain_multiplier;
	}

	std::uint32_t Synth::getBlocksPerBaseTick(ChipInstanceHandle instance) const noexcept
	{
		if (!impl_->isLive(instance))
			return 0;
		return impl_->control_slots.at(instance.getSlot().get()).program->grit.tick_multiplier;
	}

	std::uint32_t Synth::evaluateTick(ChipInstanceHandle instance,
		std::span<ParameterBlock> blocks,
		std::span<TickSubBlockSpan> sub_block_spans) noexcept
	{
		if (!impl_->isLive(instance))
			return 0;
		// `sample_rate_hz` is zero until the consumer's first `configure` call
		// (Impl's construction default). `nyquist` and `cutoff_ceiling` derive
		// from it, so no valid parameters can be computed before the sample
		// rate is known, and no blocks are emitted for this call.
		if (impl_->sample_rate_hz == 0)
			return 0;
		Impl::ControlSlotState& slot = impl_->control_slots.at(instance.getSlot().get());
		Impl::SlotState& slot_state = impl_->slot_states.at(instance.getSlot().get());
		const SoundProgram& program = *slot.program;
		const std::uint32_t multiplier = program.grit.tick_multiplier;
		const std::uint32_t count = std::min(multiplier, static_cast<std::uint32_t>(blocks.size()));
		const float nyquist = static_cast<float>(impl_->sample_rate_hz) * 0.5f;
		// `std::max` against `cutoff_hz_floor` keeps the ceiling from falling
		// below the fixed positive floor, which would be an invalid bounds
		// pair for the `std::clamp` calls below.
		// `nyquist` needs no equivalent: its own floor, `frequency_hz_floor`,
		// is 0, so `[0, nyquist]` stays a valid (possibly empty-range,
		// never inverted) pair at any sample rate.
		const float cutoff_ceiling
			= std::max(static_cast<float>(impl_->sample_rate_hz) * cutoff_hz_nyquist_margin, cutoff_hz_floor);

		// Every block of this call is evaluated at its own time: block `n`,
		// counting every block emitted for the instance, sits at scaled time
		// `n * subticks_per_base_tick`. `blocks_emitted` advances by
		// `count`, so a short `blocks` span cannot decouple gate timing from
		// table position, and a call whose blocks straddle `release_subtick`
		// gates the early blocks on and the later ones off.
		const std::uint64_t blocks_before = slot.blocks_emitted;
		slot.blocks_emitted += count;

		// Scaled time at which this instance's onset delay elapses. Zero for an instance that gated on at `start`.
		const ScaledTime onset_time = onsetActivationTime(slot.onset_delay_base_ticks, multiplier);

		// Only constructed to fill `sub_block_spans` below; unused work when
		// the caller passes none.
		const ControlGrid grid{.sample_rate_hz = impl_->sample_rate_hz};

		auto sub_block_it = sub_block_spans.begin();
		const auto sub_block_end = sub_block_spans.end();
		bool chip_gate_on = false;
		for (std::uint32_t t = 0; ParameterBlock& block : blocks.first(count))
		{
			block = ParameterBlock{};
			block.target = instance;
			block.voice_count = program.voice_count;

			const ScaledTime block_time = (blocks_before + t) * subticks_per_base_tick;
			advanceFilterAndChipGain(block,
				slot.filter_table_state,
				program.filter,
				slot.chip_output_gain,
				cutoff_ceiling,
				block_time,
				multiplier,
				slot.table_seed);
			// An external cutoff override replaces the authored value for
			// this tick, so a consumer can drive the filter from its own
			// state instead of a time-based table. A flagged zero is a genuine override, so the active flag
			// rather than a sign check gates this.
			if (slot.external_cutoff_active)
			{
				block.cutoff_hz = slot.external_cutoff_hz;
			}

			// Per-block, not per-voice: computed once here rather than
			// inside the voice loop below, since every voice in a block
			// shares the same chip-wide gate state.
			chip_gate_on = computeGateOn(program.gate, block_time, multiplier, slot.gate_held);

			const std::span<const VoiceProgram> voices(program.voices, program.voice_count);
			const std::span<VoiceParameters> block_voices(block.voices.data(), block.voice_count);
			for (std::uint32_t v = 0; v < block.voice_count; ++v)
			{
				const VoiceProgram& voice = voices.subspan(v, 1).front();
				VoiceTableState& state = slot.voice_table_state.at(v);

				// A voice holds inactive (gated off, no table evaluated)
				// until the later of its own `start_subtick` and the instance
				// onset. Neither is silently
				// dropped. On its first active block its state resets to fresh
				// base values, exactly as `start` establishes for a voice active
				// from the first block, and its table time starts counting from
				// that later time: `start_subtick` is its tables' origin, and an
				// onset delay defers the whole voice, tables included.
				const ScaledTime activation_time
					= std::max(static_cast<ScaledTime>(voice.start_subtick) * multiplier, onset_time);
				const bool voice_active = block_time >= activation_time;
				if (voice_active && !state.active)
				{
					resetVoiceTableState(state, voice);
					state.active = true;
					state.table_origin = activation_time;
				}
				if (voice_active)
				{
					advanceVoiceTables(state,
						voice,
						VoiceTableClock{.time = block_time - state.table_origin,
							.multiplier = multiplier,
							.instance_table_seed = slot.table_seed,
							.voice_index = v});
				}

				const float frequency = assembleVoiceFrequency(program.base_pitch.semitone,
					slot.pitch_variation_semitones,
					state.arpeggio_offset,
					program.base_pitch.cents,
					state.pitch_slide_accum_cents + slot.external_pitch_cents + state.pitch_offset_cents);
				const bool gate_on = voice_active && chip_gate_on && state.gate_on;

				writeVoiceBlock(block_voices.subspan(v, 1).front(), state, frequency, nyquist, gate_on);
			}

			// Grit stage: quantizes every numeric field to the playing
			// instance's grit profile, scaled by the global grit scale
			// factor. All lo-fi character is applied here, as a pass on the
			// evaluated block, keeping the synthesis engine itself
			// character-neutral.
			applyGrit(block, program.grit, impl_->grit_scale);

			clampParameterBlock(block, nyquist, cutoff_ceiling);

			if (sub_block_it != sub_block_end)
			{
				const std::uint64_t tick_index = blocks_before + t;
				*sub_block_it = TickSubBlockSpan{.start = grid.subBlockForTick(tick_index, multiplier),
					.next_start = grid.subBlockForTick(tick_index + 1, multiplier)};
				++sub_block_it;
			}
			++t;
		}
		if (count > 0)
		{
			slot_state.chip_gate_open.store(chip_gate_on);
			if (std::ranges::any_of(blocks.first(count),
					[](const ParameterBlock& block)
					{
						return std::ranges::any_of(
							std::span<const VoiceParameters>(block.voices.data(), block.voice_count),
							[](const VoiceParameters& voice) { return voice.gate_on; });
					}))
			{
				slot_state.gate_emitted.store(true);
			}
		}
		return count;
	}

	std::uint32_t Synth::getChannelCount() const noexcept
	{
		const Mixer& mixer = impl_->mixer != nullptr ? *impl_->mixer : impl_->default_mixer;
		return mixer.getChannelCount();
	}

	void Synth::applyBlock(const ParameterBlock& block) noexcept
	{
		if (!impl_->isLive(block.target))
			return;
		Impl::RenderSlotState& slot = impl_->render_slots.at(block.target.getSlot().get());
		slot.current_block = block;
		slot.renderer->applyBlock(block);
		if (std::ranges::any_of(std::span<const VoiceParameters>(block.voices.data(), block.voice_count),
				[](const VoiceParameters& voice) { return voice.gate_on; }))
		{
			impl_->slot_states.at(block.target.getSlot().get()).gate_opened.store(true);
		}
	}

	void Synth::render(std::span<float> output) noexcept
	{
		DenormalGuard guard;

		Mixer& mixer = impl_->mixer != nullptr ? *impl_->mixer : impl_->default_mixer;
		const std::uint32_t channels = mixer.getChannelCount();
		const std::uint32_t frames = static_cast<std::uint32_t>(output.size()) / channels;
		assert(frames % sub_block_frame_count == 0);

		const std::uint32_t sub_blocks = frames / sub_block_frame_count;
		// The contract documented on `render` is a whole number of sub-blocks.
		// The assert above is compiled out in release builds, so a violated
		// contract must still honor "previous contents are overwritten":
		// the whole sub-blocks are rendered and the trailing partial sub-block
		// is zeroed rather than left holding stale data.
		const std::uint32_t rendered_frames = sub_blocks * sub_block_frame_count;
		if (rendered_frames != frames)
			std::fill(output.begin() + static_cast<std::ptrdiff_t>(rendered_frames * channels), output.end(), 0.0f);

		for (std::uint32_t sub = 0; sub < sub_blocks; ++sub)
		{
			const std::uint32_t block_start = sub * sub_block_frame_count * channels;
			std::span<float> block_output = output.subspan(block_start, sub_block_frame_count * channels);
			mixer.beginBlock(block_output, sub_block_frame_count);

			for (std::uint32_t s = 0; s < impl_->render_slots.size(); ++s)
			{
				Impl::SlotState& state = impl_->slot_states.at(s);
				const std::uint32_t generation = state.generation.load();
				if (generation % 2 == 0)
				{
					// Free: relinquish and acknowledge. This is what lets `start`
					// reclaim the slot. It must never touch the renderer below for an
					// even generation.
					if (state.retired_generation.load() != generation)
						state.retired_generation.store(generation);
					continue;
				}
				Impl::RenderSlotState& slot = impl_->render_slots.at(s);
				// Drive one renderer per live instance, applying the
				// instance's block before rendering. `BlockRenderer` is the one
				// renderer offline callers drive too, so the offline result has
				// no second implementation to drift from.
				slot.renderer->applyBlock(slot.current_block);
				slot.renderer->render(slot.staging);
				mixer.submitInstance(ChipInstanceHandle(s, generation), slot.staging);
				if (slot.renderer->isComplete() && !state.chip_gate_open.load()
					&& (state.gate_opened.load() || !state.gate_emitted.load()))
					state.completed_generation.store(generation);
			}

			mixer.endBlock();
		}
	}
}
