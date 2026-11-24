#pragma once

#include <cstdint>
#include <limits>

namespace glirps::impl
{
	// Deterministic per-chip-instance generator for seeded pitch variation
	// (every LFSR and every variation RNG is seeded per chip instance, so
	// a render is reproducible from its seed) and for random table value
	// draws, one short-lived generator per draw. A splitmix64 step: a full
	// 64-bit permutation with a 2^64 period and no known statistical
	// weakness, so both a single draw per instance and the one draw per
	// freshly seeded random table value are equally well supported;
	// nothing about splitmix64's construction favors a first draw over a
	// later one in the same sequence.
	//
	// Satisfies `UniformRandomBitGenerator` (`operator()`, `result_type`,
	// `min()`, `max()`), so it plugs directly into
	// `std::uniform_real_distribution`/`std::uniform_int_distribution`.
	class VariationRng
	{
	public:
		// `UniformRandomBitGenerator` result type, matching `operator()`'s return.
		using result_type = std::uint64_t;

		// Constructs a generator whose draw sequence is fixed by `seed`.
		explicit VariationRng(std::uint64_t seed) noexcept
			: state_(seed)
		{
		}

		// Advances the generator and returns the next draw.
		[[nodiscard]] std::uint64_t next() noexcept
		{
			state_ += golden_ratio_increment;
			std::uint64_t mixed = state_;
			mixed = (mixed ^ (mixed >> first_shift)) * mix_constant_1;
			mixed = (mixed ^ (mixed >> second_shift)) * mix_constant_2;
			return mixed ^ (mixed >> final_shift);
		}

		// `UniformRandomBitGenerator` entry point; equivalent to `next()`.
		[[nodiscard]] std::uint64_t operator()() noexcept { return next(); }

		// `UniformRandomBitGenerator` lower bound of `operator()`'s range.
		[[nodiscard]] static constexpr std::uint64_t min() noexcept
		{
			return std::numeric_limits<std::uint64_t>::min();
		}

		// `UniformRandomBitGenerator` upper bound of `operator()`'s range.
		[[nodiscard]] static constexpr std::uint64_t max() noexcept
		{
			return std::numeric_limits<std::uint64_t>::max();
		}

	private:
		static constexpr std::uint64_t golden_ratio_increment = 0x9e3779b97f4a7c15ULL;
		static constexpr std::uint64_t mix_constant_1 = 0xbf58476d1ce4e5b9ULL;
		static constexpr std::uint64_t mix_constant_2 = 0x94d049bb133111ebULL;
		// The splitmix64 xor-shift distances, in bits.
		static constexpr std::uint32_t first_shift = 30;
		static constexpr std::uint32_t second_shift = 27;
		static constexpr std::uint32_t final_shift = 31;

		std::uint64_t state_;
	};
}
