#pragma once

/// @file
/// GLIRPS's public API for BlockRenderer: free functions in namespace
/// `glirps` that create, configure, drive, and render one chip instance
/// directly, with no `Synth` pool involved and no mixer seam. The one owning
/// resource is the opaque `BlockRendererHandle`; same binary-compatibility
/// design as glirps.hpp -- no RAII type on this surface, a raw handle plus
/// explicit create/destroy stands in for a constructor/destructor pair.

#include "BlockRendererHandle.hpp"
#include "Environment.hpp"
#include "FilterMode.hpp"
#include "ParameterBlock.hpp"
#include "Span.hpp"
#include "Waveform.hpp"
#include "constants.hpp"

#include <cstdint>

namespace glirps
{
	/// Why `blockRendererCreate` did not produce a usable handle. Meaningful
	/// only when the result is falsy.
	enum class BlockRendererCreateError : std::uint8_t
	{
		/// The renderer could not be allocated.
		allocation_failure,
	};

	/// The outcome of `blockRendererCreate`, returned by value in place of a
	/// null handle so a failure carries its reason. On success `renderer` is a
	/// valid handle; on failure `renderer` is null. Trivially copyable and
	/// standard-layout.
	struct BlockRendererCreateResult final
	{
		/// The created handle on success; null on failure.
		BlockRendererHandle renderer;
		/// The reason for failure. Meaningful only when the result is falsy.
		BlockRendererCreateError error;

		/// Reports whether creation succeeded, derived from the handle so no
		/// success value is added to `BlockRendererCreateError`.
		[[nodiscard]] explicit operator bool() const noexcept { return renderer != nullptr; }
	};

	/// @defgroup blockRenderer Rendering
	/// Free functions that create and drive a block renderer.
	///
	/// **Threading.** GLIRPS creates no threads and holds no locks. A
	/// `BlockRendererHandle` has no synchronization of its own and belongs to one
	/// thread at a time: no two calls on the same handle SHALL overlap, and
	/// handing a handle to another thread needs a happens-before relation that
	/// the consumer provides. Different handles are independent.
	///
	/// A consumer rendering in an audio thread calls every function on a handle
	/// from that thread, except setup done before the audio thread starts using
	/// the handle, such as `blockRendererCreate` and `blockRendererConfigureVoice`,
	/// and `blockRendererDestroy` after it has stopped. To change parameters from
	/// another thread, the consumer passes the values to the audio thread, for
	/// example as `ParameterBlock` values for `blockRendererApplyBlock`, instead of
	/// calling the setters directly.
	///
	/// **Voices.** A block renderer holds `max_voices_per_instance` voices, numbered from 0. Every `voice_index`
	/// SHALL be below that; an out-of-range index terminates the program.
	///
	/// **Blocking.** No function waits on another thread. `blockRendererCreate`,
	/// `blockRendererDestroy`, and `blockRendererReconfigure` may block in the
	/// memory allocator or in the consumer's allocator callbacks, so a
	/// real-time consumer calls them only while the audio thread is not
	/// rendering. Any function MAY call the consumer's log callback; see
	/// `LogCallbacks`. Computation and atomic operations do not count as blocking.

	/// Creates a renderer at `sample_rate_hz`, deriving every rate-dependent
	/// constant once. Fails only on allocation failure; the surface does not
	/// throw.
	///
	/// The renderer allocates through `environment->allocator` and reports to
	/// `environment->logger`, both for its whole life. A null `environment`
	/// selects the default C++ allocation and no logging. The environment is
	/// copied, so it need not outlive this call, but its callbacks and their
	/// contexts SHALL stay valid until @ref blockRendererDestroy returns.
	/// @param sample_rate_hz The output sample rate, in hertz.
	/// @param oversampling_factor The internal oversampling factor, a positive integer. Oscillators, envelopes, and
	///   filter run at this multiple of `sample_rate_hz`, so CPU cost grows with it.
	/// @param environment Allocator and log callbacks, or null for none.
	/// @return The created handle, or a falsy result stating why creation failed.
	/// @threading The allocator and log callbacks run on the calling thread during this call.
	/// @ingroup blockRenderer
	[[nodiscard]] BlockRendererCreateResult blockRendererCreate(std::uint32_t sample_rate_hz,
		std::uint32_t oversampling_factor,
		const Environment* environment = nullptr) noexcept;

	/// Destroys a renderer. Null-safe. The allocator and log callbacks given to @ref blockRendererCreate are not used
	/// after this call returns.
	/// @param renderer The renderer to destroy; may be null.
	/// @threading The allocator and log callbacks run on the calling thread during this call.
	/// @ingroup blockRenderer
	void blockRendererDestroy(BlockRendererHandle renderer) noexcept;

	/// Recomputes every rate-dependent constant for `sample_rate_hz` and
	/// `oversampling_factor`.
	/// @param renderer The renderer to reconfigure.
	/// @param sample_rate_hz The output sample rate, in hertz.
	/// @param oversampling_factor The internal oversampling factor, a positive integer. Oscillators, envelopes, and
	///   filter run at this multiple of `sample_rate_hz`, so CPU cost grows with it.
	/// @threading An `oversampling_factor` above the one given to @ref blockRendererCreate makes the renderer
	///   allocate larger
	///   buffers through the allocator callbacks of its environment, and a null return from them then terminates
	///   the program.
	/// @ingroup blockRenderer
	void blockRendererReconfigure(BlockRendererHandle renderer,
		std::uint32_t sample_rate_hz,
		std::uint32_t oversampling_factor) noexcept;

	/// Returns the renderer to a cleared state: every voice
	/// inactive, oscillator and envelope zeroed, filter and decimator history
	/// cleared.
	/// @param renderer The renderer to reset.
	/// @ingroup blockRenderer
	void blockRendererResetState(BlockRendererHandle renderer) noexcept;

	/// Reseeds one voice's noise register from `seed`, leaving every other
	/// voice field untouched.
	/// @param renderer The renderer owning the voice.
	/// @param voice_index The voice to reseed.
	/// @param seed The new noise seed.
	/// @ingroup blockRenderer
	void
	blockRendererSeedVoiceNoise(BlockRendererHandle renderer, std::uint32_t voice_index, std::uint32_t seed) noexcept;

	/// Configures one voice directly: waveform, frequency, pulse width, gain, and noise seed. It also makes the voice
	/// take part in rendering. The envelope still needs a gate before the voice is heard.
	/// @param renderer The renderer owning the voice.
	/// @param voice_index The voice to configure.
	/// @param waveform The voice's waveform.
	/// @param frequency_hz The oscillator frequency, in hertz.
	/// @param pulse_width The pulse duty cycle in `[pulse_width_floor, pulse_width_ceiling]`.
	/// @param gain The voice gain as a linear multiplier.
	/// @param seed The noise seed.
	/// @ingroup blockRenderer
	void blockRendererConfigureVoice(BlockRendererHandle renderer,
		std::uint32_t voice_index,
		Waveform waveform,
		float frequency_hz,
		float pulse_width,
		float gain,
		std::uint64_t seed) noexcept;

	/// Selects one voice's envelope rate indices and sustain level. Each index, 0 to 15, selects one of 16 presets.
	/// Attack runs from 2 ms at index 0 to 8 s at index 15, and the decay and release times of an index are three
	/// times its attack time. An index of 16 or more is ignored and leaves the envelope unchanged.
	/// `blockRendererSetVoiceEnvelopeSeconds` sets the times directly.
	/// @param renderer The renderer owning the voice.
	/// @param voice_index The voice to configure.
	/// @param attack_index Attack rate index, 0 to 15.
	/// @param decay_index Decay rate index, 0 to 15.
	/// @param release_index Release rate index, 0 to 15.
	/// @param sustain_level Sustain level as a linear multiplier.
	/// @ingroup blockRenderer
	void blockRendererSetVoiceEnvelope(BlockRendererHandle renderer,
		std::uint32_t voice_index,
		std::uint32_t attack_index,
		std::uint32_t decay_index,
		std::uint32_t release_index,
		float sustain_level) noexcept;

	/// Selects one voice's envelope from durations in seconds and a sustain
	/// level, converted to per-sample rates once.
	/// @param renderer The renderer owning the voice.
	/// @param voice_index The voice to configure.
	/// @param attack_seconds Attack duration, in seconds.
	/// @param decay_seconds Decay duration, in seconds.
	/// @param release_seconds Release duration, in seconds.
	/// @param sustain_level Sustain level as a linear multiplier.
	/// @ingroup blockRenderer
	void blockRendererSetVoiceEnvelopeSeconds(BlockRendererHandle renderer,
		std::uint32_t voice_index,
		float attack_seconds,
		float decay_seconds,
		float release_seconds,
		float sustain_level) noexcept;

	/// Gates one voice's envelope.
	/// @param renderer The renderer owning the voice.
	/// @param voice_index The voice to gate.
	/// @param gate_on True to gate the envelope on, false to release it.
	/// @ingroup blockRenderer
	void blockRendererSetGate(BlockRendererHandle renderer, std::uint32_t voice_index, bool gate_on) noexcept;

	/// Sets one voice's oscillator frequency (used for sweeps).
	/// @param renderer The renderer owning the voice.
	/// @param voice_index The voice to update.
	/// @param frequency_hz The new oscillator frequency, in hertz.
	/// @ingroup blockRenderer
	void blockRendererSetVoiceFrequency(BlockRendererHandle renderer,
		std::uint32_t voice_index,
		float frequency_hz) noexcept;

	/// Enables ring modulation on one voice from another voice's oscillator.
	/// @param renderer The renderer owning the voices.
	/// @param voice_index The voice that is ring-modulated.
	/// @param source_index The voice whose oscillator modulates it.
	/// @ingroup blockRenderer
	void blockRendererSetRingModSource(BlockRendererHandle renderer,
		std::uint32_t voice_index,
		std::uint32_t source_index) noexcept;

	/// Enables hard sync on one voice from another voice's oscillator overflow.
	/// @param renderer The renderer owning the voices.
	/// @param voice_index The voice that is synced.
	/// @param source_index The voice whose oscillator overflow resets it.
	/// @ingroup blockRenderer
	void blockRendererSetHardSyncSource(BlockRendererHandle renderer,
		std::uint32_t voice_index,
		std::uint32_t source_index) noexcept;

	/// Configures the shared filter.
	/// @param renderer The renderer to configure.
	/// @param cutoff_hz Filter cutoff, in hertz.
	/// @param resonance_q Filter resonance as a Q factor.
	/// @param mode The filter response to emit.
	/// @param saturation Saturation drive in `[0, 1]`.
	/// @ingroup blockRenderer
	void blockRendererSetFilter(BlockRendererHandle renderer,
		float cutoff_hz,
		float resonance_q,
		FilterMode mode,
		float saturation) noexcept;

	/// Updates only the shared filter cutoff (used for sweeps).
	/// @param renderer The renderer to update.
	/// @param cutoff_hz The new filter cutoff, in hertz.
	/// @ingroup blockRenderer
	void blockRendererSetFilterCutoff(BlockRendererHandle renderer, float cutoff_hz) noexcept;

	/// Sets the shared filter's saturation. Not changed by
	/// `blockRendererApplyBlock` or `blockRendererApplyBaseBlock`:
	/// `ParameterBlock` carries no saturation field.
	/// @param renderer The renderer to update.
	/// @param saturation The new saturation drive in `[0, 1]`.
	/// @ingroup blockRenderer
	void blockRendererSetFilterSaturation(BlockRendererHandle renderer, float saturation) noexcept;

	/// Sets the shared output gain.
	/// @param renderer The renderer to update.
	/// @param gain The new output gain as a linear multiplier.
	/// @ingroup blockRenderer
	void blockRendererSetOutputGain(BlockRendererHandle renderer, float gain) noexcept;

	/// Returns the decimator's group delay in output frames, so a measurement
	/// can align renders at different oversampling factors.
	/// @param renderer The renderer to query.
	/// @return The decimator group delay, in output frames.
	/// @ingroup blockRenderer
	[[nodiscard]] std::uint32_t blockRendererGetDecimatorGroupDelayInFrames(BlockRendererHandle renderer) noexcept;

	/// Installs `block`'s parameters on the instance: waveform, frequency,
	/// pulse width, filter modes, and gate step immediately; cutoff,
	/// resonance, per-voice gain, and output gain ramp to their new values
	/// over one sub-block.
	/// @param renderer The renderer to update.
	/// @param block The parameters to install.
	/// @ingroup blockRenderer
	void blockRendererApplyBlock(BlockRendererHandle renderer, const ParameterBlock& block) noexcept;

	/// Snaps every field of `block` directly with no ramp, establishing the
	/// instance's base state.
	/// @param renderer The renderer to update.
	/// @param block The parameters to snap to.
	/// @ingroup blockRenderer
	void blockRendererApplyBaseBlock(BlockRendererHandle renderer, const ParameterBlock& block) noexcept;

	/// Renders `output.size()` frames of mono, which SHALL be a whole number of sub-blocks. Allocates nothing, takes
	/// no lock, issues no syscall. Unlike `synthRender`, it does not change the floating-point mode; a consumer that
	/// wants flush-to-zero sets it on the render thread.
	/// @param renderer The renderer to run.
	/// @param output Caller-owned mono buffer to fill.
	/// @ingroup blockRenderer
	void blockRendererRender(BlockRendererHandle renderer, shims::Span<float> output) noexcept;

	/// Advances one sample and returns it, for a caller that sweeps a
	/// parameter every sample.
	/// @param renderer The renderer to advance.
	/// @return The rendered sample.
	/// @ingroup blockRenderer
	[[nodiscard]] float blockRendererRenderOne(BlockRendererHandle renderer) noexcept;

	/// Returns true when every voice of the renderer has finished.
	/// @param renderer The renderer to query.
	/// @return Whether every voice has finished.
	/// @ingroup blockRenderer
	[[nodiscard]] bool blockRendererIsComplete(BlockRendererHandle renderer) noexcept;
}
