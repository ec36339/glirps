#pragma once

#include "Allocator.hpp"
#include "glirps/FilterMode.hpp"
#include "glirps/SoundProgram.hpp"
#include "glirps/Waveform.hpp"
#include "glirps/constants.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace glirps
{
	// Defined in ParameterBlock.hpp.
	struct ParameterBlock;
}

namespace glirps::impl
{
	// Default render rate for an offline caller with no device rate to supply.
	inline constexpr std::uint32_t default_render_sample_rate_hz = 48000U;

	// Renders one chip instance to a mono block. Owns that instance's oscillator
	// phase, envelope state, filter state, noise register, decimator history,
	// parameter ramps, and the constants derived from its sample rate and
	// oversampling factor. It owns no table state and no grid phase; table
	// state belongs to the control-side evaluator.
	//
	// Declared rather than hidden because a consumer with no `Synth` renders
	// through it directly.
	// `Synth::render` drives one per live instance
	// and submits each result to the installed `Mixer`.
	class BlockRenderer
	{
	public:
		// Constructs a renderer at `sample_rate_hz`, deriving every
		// rate-dependent constant once. `oversampling_factor` selects the
		// internal oversampling ratio ahead of the decimator. Every allocation
		// goes through `allocator`; a failed one throws `std::bad_alloc`.
		explicit BlockRenderer(std::uint32_t sample_rate_hz = default_render_sample_rate_hz,
			std::uint32_t oversampling_factor = default_oversampling_factor,
			const AllocatorRef& allocator = AllocatorRef());

		~BlockRenderer();
		BlockRenderer(const BlockRenderer&) = delete;
		BlockRenderer& operator=(const BlockRenderer&) = delete;
		BlockRenderer(BlockRenderer&&) = delete;
		BlockRenderer& operator=(BlockRenderer&&) = delete;

		// Recomputes every rate-dependent constant for `sample_rate_hz` and
		// `oversampling_factor`.
		void reconfigure(std::uint32_t sample_rate_hz, std::uint32_t oversampling_factor) noexcept;

		// Returns the renderer to a cleared state: every voice
		// inactive, oscillator and envelope zeroed, filter and decimator
		// history cleared. Called by `Synth::start` when a reused slot is
		// handed to a new instance, so no DSP state from the previous
		// occupant survives into the new sound.
		void resetState() noexcept;

		// Reseeds one voice's noise register from `seed`, leaving every other
		// voice field untouched. Used by `Synth::start` to seed every voice
		// of a reused slot's renderer from the instance's own
		// `instance_seed`, so a given seed always reproduces the same sound.
		void seedVoiceNoise(std::uint32_t voice_index, std::uint32_t seed) noexcept;

		// Configures one voice directly: waveform, frequency, pulse width, gain,
		// and noise seed. Direct entry point, also available as a free
		// function in `glirps_render.hpp`; `applyBlock` is the data-driven path.
		void configureVoice(std::uint32_t voice_index,
			Waveform waveform,
			float frequency_hz,
			float pulse_width,
			float gain,
			std::uint64_t seed) noexcept;

		// Selects one voice's envelope rate indices and sustain level. Direct
		// entry point, also available as a free function in `glirps_render.hpp`.
		void setVoiceEnvelope(std::uint32_t voice_index,
			std::uint32_t attack_index,
			std::uint32_t decay_index,
			std::uint32_t release_index,
			float sustain_level) noexcept;

		// Selects one voice's envelope from durations in seconds and a sustain
		// level, converted to per-sample rates once.
		void setVoiceEnvelopeSeconds(std::uint32_t voice_index,
			float attack_seconds,
			float decay_seconds,
			float release_seconds,
			float sustain_level) noexcept;

		// Gates one voice's envelope. Direct entry point, also
		// available as a free function in `glirps_render.hpp`.
		void setGate(std::uint32_t voice_index, bool gate_on) noexcept;

		// Sets one voice's oscillator frequency (used for sweeps). Also
		// available as a free function in `glirps_render.hpp`.
		void setVoiceFrequency(std::uint32_t voice_index, float frequency_hz) noexcept;

		// Enables ring modulation on one voice from another voice's oscillator.
		// Called by `Synth::start` to install the program's routing; also
		// available as a free function in `glirps_render.hpp`.
		void setRingModSource(std::uint32_t voice_index, std::uint32_t source_index) noexcept;

		// Enables hard sync on one voice from another voice's oscillator
		// overflow. Called by `Synth::start` to install the program's routing;
		// also available as a free function in `glirps_render.hpp`.
		void setHardSyncSource(std::uint32_t voice_index, std::uint32_t source_index) noexcept;

		// Configures the shared filter. Direct entry point, also
		// available as a free function in `glirps_render.hpp`.
		void setFilter(float cutoff_hz, float resonance_q, FilterMode mode, float saturation) noexcept;

		// Updates only the shared filter cutoff (used for sweeps). Also
		// available as a free function in `glirps_render.hpp`.
		void setFilterCutoff(float cutoff_hz) noexcept;

		// Sets the shared output gain. Direct entry point, also
		// available as a free function in `glirps_render.hpp`.
		void setOutputGain(float gain) noexcept;

		// The decimator's group delay in output frames, so a measurement can
		// align renders at different oversampling factors.
		[[nodiscard]] std::uint32_t getDecimatorGroupDelayInFrames() const noexcept;

		// Installs `block`'s parameters on the instance: waveform, frequency,
		// pulse width, filter modes, and gate step immediately, because only
		// cutoff, resonance, and gain are smoothed, and smoothing pitch would
		// destroy the stepped arpeggio character; cutoff, resonance, per-voice
		// gain, and output gain ramp to their new values over one sub-block
		// to avoid zipper noise.
		void applyBlock(const ParameterBlock& block) noexcept;

		// Snaps every field of `block` directly with no ramp, establishing the
		// instance's base state, so the first render does not ramp in from
		// whatever a reused slot's renderer last held.
		void applyBaseBlock(const ParameterBlock& block) noexcept;

		// Sets the shared filter's saturation. Fixed for the instance's whole
		// life: `ParameterBlock` carries no per-tick saturation field, so
		// `Synth::start` sets it once from the program rather than per block.
		void setFilterSaturation(float saturation) noexcept;

		// Renders `output.size()` frames of mono, which SHALL be a whole number
		// of sub-blocks. Allocates nothing, takes no lock, issues no syscall.
		void render(std::span<float> output) noexcept;

		// Advances one sample and returns it, for a caller that sweeps a
		// parameter every sample. Also available as a free function in `glirps_render.hpp`.
		float renderOne() noexcept;

		// Returns true once every active voice's envelope has finished.
		[[nodiscard]] bool isComplete() const noexcept;

	private:
		struct Impl;
		UniquePtr<Impl> impl_;
	};
}
