#pragma once

/// @file
/// Handle types naming one playing chip instance.

#include <cstdint>

namespace glirps
{
	/// A chip instance's pool slot, distinct from a bare integer so it cannot
	/// be confused with an unrelated count or index at a module boundary.
	/// `ChipInstanceHandle::getSlot()` is its only source; a consumer extracts the
	/// raw value with `.get()` to index its own per-slot table.
	class ChipSlotIndex
	{
	public:
		ChipSlotIndex() = default;

		/// Creates a slot index holding `value`.
		/// @param value The raw slot number.
		constexpr explicit ChipSlotIndex(std::uint32_t value) noexcept
			: value_(value)
		{
		}

		/// Returns the raw slot number.
		/// @return The raw slot number.
		[[nodiscard]] constexpr std::uint32_t get() const noexcept { return value_; }

		/// Compares two slot indices by value.
		[[nodiscard]] friend bool operator==(ChipSlotIndex, ChipSlotIndex) noexcept = default;

	private:
		std::uint32_t value_ = 0;
	};

	/// Identifies one playing chip instance. Trivially copyable and compared by
	/// value. Issued by `synthStart` and invalidated by `synthRelease` or `synthConfigure`.
	class ChipInstanceHandle
	{
	public:
		ChipInstanceHandle() = default;

		/// Compares two handles by value; equal only when slot and generation match.
		[[nodiscard]] friend bool operator==(ChipInstanceHandle, ChipInstanceHandle) noexcept = default;

		/// Returns true for a handle that never named an instance.
		/// @return True when the handle never named an instance.
		[[nodiscard]] bool isNull() const noexcept { return generation_ == 0; }

		/// Returns the pool slot this handle names, so a consumer can index a
		/// fixed per-slot table by it, for example a mixer's per-slot gain
		/// table read on the callback thread. A consumer gates staleness
		/// with `operator==` against the handle it stored, which rejects a reused
		/// slot.
		/// @return The pool slot index.
		[[nodiscard]] ChipSlotIndex getSlot() const noexcept { return ChipSlotIndex(slot_); }

		/// Returns the generation of the slot this handle names. A slot's
		/// generation changes each time the slot is reused, so a handle whose
		/// generation no longer matches the slot's is expired.
		/// @return The generation; zero for a null handle.
		[[nodiscard]] std::uint32_t getGeneration() const noexcept { return generation_; }

		/// Creates a handle naming `slot` at `generation`. The synth mints
		/// these; a consumer obtains handles from `synthStart` and never needs
		/// this constructor.
		/// @param slot The pool slot the handle names.
		/// @param generation The slot's generation; zero is the null handle.
		ChipInstanceHandle(std::uint32_t slot, std::uint32_t generation) noexcept
			: slot_(slot)
			, generation_(generation)
		{
		}

	private:
		std::uint32_t slot_ = 0;
		std::uint32_t generation_ = 0;
	};
}
