#pragma once

#include "Allocator.hpp"
#include "Mixer.hpp"
#include "glirps/ChipInstanceHandle.hpp"
#include "glirps/ControlParams.hpp"
#include "glirps/ParameterBlock.hpp"
#include "glirps/SoundProgram.hpp"
#include "glirps/StartError.hpp"

#include <cstdint>
#include <expected>
#include <memory>
#include <span>

namespace glirps::impl
{
	// Backend of the public synth API. Owns the chip instance and voice
	// pools, the table state of every playing instance, and all per-voice DSP
	// state.
	//
	// Entry points are labeled control side or render side. Apart from the
	// slot handoff atomics, the once-per-claim setup `start` does, and
	// `configure` and `setMixer`, which must not overlap `render`, the two
	// sets touch disjoint state, and the consumer decides which thread runs
	// each set. This type owns no thread, no device, and no transport.
	class Synth
	{
	public:
		// Allocates the chip instance and voice pools at the given fixed
		// sizes. Only construction and `configure` may allocate, through
		// `allocator`. A failed allocation throws `std::bad_alloc` from the
		// constructor and terminates the program in `configure`.
		Synth(std::uint32_t max_chip_instances,
			std::uint32_t max_voices,
			const AllocatorRef& allocator = AllocatorRef());

		~Synth();
		Synth(const Synth&) = delete;
		Synth& operator=(const Synth&) = delete;
		Synth(Synth&&) = delete;
		Synth& operator=(Synth&&) = delete;

		// Derives every rate-dependent constant. Control side. Must be called
		// before the first `render` and again whenever the rate changes.
		// Discards every instance in flight.
		void configure(std::uint32_t sample_rate_hz) noexcept;

		// As above, with an explicit oversampling factor.
		void configure(std::uint32_t sample_rate_hz, std::uint32_t oversampling_factor) noexcept;

		// Sets the grit scale, from 0 (no quantization) to 1 (the program's
		// grit profile, the default). Control side; takes effect on the next
		// `evaluateTick`.
		void setGritScale(float scale) noexcept;

		// Installs the mixer `render` submits each chip instance's block to,
		// which must outlive this object. Without a call, the built-in
		// `SumToMonoMixer` is used. Control side, and must not be called while
		// a render is in flight.
		void setMixer(Mixer& mixer) noexcept;

		// Starts one chip instance playing `program`, seeded by
		// `instance_seed`. Every voice whose `start_subtick` is zero is gated on
		// and renders from its base
		// values until a block replaces them. `Synth` stores a non-owning
		// pointer to `program` and allocates nothing for it, since copying it
		// would cost an allocation per `start`: the caller must guarantee
		// `program` outlives the instance until `release`, `configure`, or
		// `~Synth`. Control side.
		//
		// `onset_delay_base_ticks` withholds the instance's onset so the onset
		// lands on the base control grid. Base ticks are 1-indexed here: 0,
		// the default, gates on at `start` every voice whose `start_subtick` is
		// zero; 1 holds every voice
		// inactive until the first tick `evaluateTick` emits, which is the
		// first block carrying a grid-derived sample position, so the gate opens
		// on the control grid rather than at the start of the next `render`; N
		// holds until the Nth emitted base tick. A held voice is inactive
		// exactly as a voice deferred by `start_subtick` is: gate off,
		// envelope idle, no table evaluated.
		//
		// `params` establishes the instance's initial control state: its
		// flagged fields are applied to the slot before the base block is
		// written, so the instance's first evaluated block already carries
		// them. A non-finite flagged value is ignored rather than stored.
		// `params.reset_flags` is not read: a freshly claimed slot already
		// begins at the authored state it would restore.
		[[nodiscard]] std::expected<ChipInstanceHandle, StartError> start(const SoundProgram& program,
			std::uint64_t instance_seed,
			std::uint32_t onset_delay_base_ticks = 0,
			ControlParams params = {}) noexcept;

		// Returns true when every voice of the instance has finished, and for
		// an expired handle. Control side.
		[[nodiscard]] bool isComplete(ChipInstanceHandle instance) const noexcept;

		// Returns the slot to the free list. A no-op for an expired handle.
		// Control side.
		void release(ChipInstanceHandle instance) noexcept;

		// Holds or releases the gate of an instance whose program sets no
		// `release_subtick` (`has_release_subtick` false). No effect on a
		// one-shot instance or an expired handle. Control side.
		void setGate(ChipInstanceHandle instance, bool gate_on) noexcept;

		// Revises the external per-tick pitch, filter-cutoff, and gain
		// control of a live instance, applied at the next `evaluateTick`.
		// `params` carries a `ControlFlag` mask naming which fields this
		// call applies; an unflagged field keeps the instance's current
		// value, and a non-finite flagged value is ignored rather than
		// stored. `params.reset_flags` names fields to instead reset to
		// their authored state, taking precedence over `flags` for the same
		// field. Applies to a sustained instance driven by consumer state,
		// and equally to a one-shot instance, such as a pitch offset the
		// consumer applies to every outstanding instance each tick.
		// `release_subtick` governs when the gate
		// releases and says nothing about whether the instance's other
		// parameters may be driven from outside. No effect on an expired
		// handle. Control side.
		void updateControlParams(ChipInstanceHandle instance, ControlParams params) noexcept;

		// Returns the parameter blocks one base tick of this instance
		// emits, which is its effective tick multiplier. Returns zero for an
		// expired handle. Control side.
		[[nodiscard]] std::uint32_t getBlocksPerBaseTick(ChipInstanceHandle instance) const noexcept;

		// Advances the tables of one instance by one base tick
		// and writes the resulting blocks to `blocks`. Returns the number
		// written. `blocks` must hold at least `getBlocksPerBaseTick`
		// entries. Returns zero for an expired handle. Control side. Fields
		// are clamped after the grit stage, so quantization cannot round one
		// past a bound.
		// `frequency_hz` is clamped to `[0, sample_rate_hz / 2]`.
		//
		// When `sub_block_spans` is non-empty, its first `written` entries
		// are set to each emitted block's `TickSubBlockSpan`.
		[[nodiscard]] std::uint32_t evaluateTick(ChipInstanceHandle instance,
			std::span<ParameterBlock> blocks,
			std::span<TickSubBlockSpan> sub_block_spans = {}) noexcept;

		// Installs `block` on the instance it targets. Ignored for an expired
		// handle. Render side.
		void applyBlock(const ParameterBlock& block) noexcept;

		// Returns the installed mixer's channel count, 1 for the default
		// `SumToMonoMixer`. Forwards to `Mixer::getChannelCount`, which owns
		// the output channel topology; `Synth` neither chooses nor
		// stores it. Exists so a consumer holding only a `Synth&` can size
		// the span it passes to `render` without naming a mixer type.
		[[nodiscard]] std::uint32_t getChannelCount() const noexcept;

		// Renders every active chip instance and submits each to the installed
		// mixer, which writes `output`. `output` is interleaved at the mixer's
		// channel count, and the frame count it implies must be a whole number
		// of sub-blocks. Its previous contents are overwritten, never
		// accumulated into. Allocates nothing, takes no lock, issues no
		// syscall, and logs nothing. Render side.
		void render(std::span<float> output) noexcept;

	private:
		struct Impl;
		UniquePtr<Impl> impl_;
	};
}
