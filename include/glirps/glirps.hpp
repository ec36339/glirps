#pragma once

/// @file
/// GLIRPS's public API: free functions in namespace `glirps` that
/// create, configure, drive, and render a synth. The one owning resource is
/// the opaque `SynthHandle`; a custom mixer is a set of C-style callbacks.

#include "ChipInstanceHandle.hpp"
#include "ControlParams.hpp"
#include "Environment.hpp"
#include "FilterMode.hpp"
#include "GritProfile.hpp"
#include "ParameterBlock.hpp"
#include "SoundProgram.hpp"
#include "Span.hpp"
#include "StartError.hpp"
#include "SynthHandle.hpp"
#include "Waveform.hpp"
#include "constants.hpp"
#include "frequency.hpp"

#include <cstdint>

/// @namespace glirps
/// GLIRPS, a procedural sound-effect synthesis core: sound programs,
/// parameter blocks, and the free functions that create, drive, and render a
/// synth or a single block renderer.

/// @namespace glirps::shims
/// The C++ shims of Doxygen group `shims`: layout-guaranteed stand-ins for
/// standard library types used at the library boundary, and the RAII
/// `Handle` wrapper.

namespace glirps
{
	/// Why @ref synthCreate did not produce a usable handle. Meaningful only when
	/// the result is falsy.
	enum class CreateError : std::uint8_t
	{
		/// Either pool size argument was zero.
		invalid_arguments,
		/// The synth could not be allocated.
		allocation_failure,
	};

	/// The outcome of @ref synthCreate, returned by value in place of a null
	/// handle so a failure carries its reason. On success `synth` is a valid
	/// handle; on failure `synth` is null and `error` states why. Trivially
	/// copyable and standard-layout.
	struct CreateResult final
	{
		/// The created handle on success; null on failure.
		SynthHandle synth;
		/// The reason for failure. Meaningful only when the result is falsy.
		CreateError error;

		/// Returns true on success.
		[[nodiscard]] explicit operator bool() const noexcept { return synth != nullptr; }
	};

	/// The outcome of @ref synthStart. On success `instance` names the started
	/// instance; on failure `instance` is null and `error` states why.
	/// Trivially copyable and standard-layout.
	struct StartResult final
	{
		/// The started instance on success; a null handle on failure.
		ChipInstanceHandle instance;
		/// The reason for failure. Meaningful only when the result is falsy.
		StartError error;

		/// Returns true on success.
		[[nodiscard]] explicit operator bool() const noexcept { return !instance.isNull(); }
	};

	/// A consumer mixer: C-style callbacks and the context passed to them. Trivially copyable and standard-layout.
	///
	/// Each callback runs synchronously on the thread that calls the entry
	/// function, with no lock held by GLIRPS. The callbacks and `context` must
	/// stay safe to call from the moment @ref synthSetMixer installs them until the
	/// next call to @ref synthSetMixer or @ref synthDestroy.
	///
	/// - `begin_block`, `submit_instance`, and `end_block` are called only from
	///   @ref synthRender, so only from the render side. They are serialized, in the
	///   order `begin_block`, one `submit_instance` per instance, `end_block`.
	/// - `get_channel_count` is called from @ref synthRender (render side) and from
	///   @ref synthGetChannelCount (control side). It may therefore run concurrently
	///   with itself and with the other callbacks, and must not read or write
	///   state that another callback modifies. It must return the same value
	///   for the whole time the mixer is installed.
	struct MixerCallbacks final
	{
		/// Opaque state handed back to every callback.
		void* context = nullptr;
		/// Returns the interleaved channels this mixer writes per frame. Called from
		/// @ref synthRender and @ref synthGetChannelCount.
		std::uint32_t (*get_channel_count)(void* context) noexcept = nullptr;
		/// Begins one sub-block into `output`, which holds
		/// `frame_count * get_channel_count()` samples. Called from @ref synthRender.
		void (*begin_block)(void* context, shims::Span<float> output, std::uint32_t frame_count) noexcept = nullptr;
		/// Submits one instance's mono block for the block in progress; `mono`
		/// is valid only for the duration of the call. Called from @ref synthRender.
		void (*submit_instance)(void* context, ChipInstanceHandle instance, shims::Span<const float> mono) noexcept
			= nullptr;
		/// Completes the block begun by `begin_block`. Called from @ref synthRender.
		void (*end_block)(void* context) noexcept = nullptr;
	};

	/// @defgroup synth Synthesis
	/// Free functions that create and drive a synth.
	///
	/// **Threading.** GLIRPS creates no threads and holds no locks. The consumer
	/// decides which thread makes each call. The functions fall into two sides:
	/// - The *control side* decides what is heard, for example @ref synthStart,
	///   @ref synthRelease, and @ref synthEvaluateTick.
	/// - The *render side* produces samples, for example @ref synthRender.
	///
	/// Calls on the same side must not overlap; the caller serializes them. One
	/// control-side call may overlap one render-side call, for example
	/// @ref synthRelease during @ref synthRender. @ref synthDestroy and @ref synthConfigure must
	/// not overlap any other call on the same synth, and @ref synthSetMixer must not
	/// overlap @ref synthRender.
	///
	/// Each function's *Threading* section names its side and lists any further
	/// thread and lifetime rules; the Threading Reference page collects them.
	///
	/// An instance handle is *live* from a successful @ref synthStart until
	/// @ref synthRelease or @ref synthConfigure. Any other handle, including a null one,
	/// is *expired*.
	///
	/// **Blocking.** No function waits on another thread, unless its Threading
	/// section says so. Computation and atomic operations do not count as
	/// blocking.

	/// Creates a synth owning chip-instance and voice pools of the given fixed
	/// sizes. Fails on invalid arguments (`max_chip_instances == 0` or
	/// `max_voices == 0`) or on allocation failure.
	/// @param max_chip_instances Size of the chip-instance pool; must be non-zero.
	/// @param max_voices Size of the voice pool; must be non-zero.
	/// @param environment Null selects default allocation and no logging. See @ref Environment.
	/// @return The created handle, or a falsy result stating why creation failed.
	/// @threading Called from the control side. May block in the memory allocator.
	/// @relates CreateResult
	/// @ingroup synth
	[[nodiscard]] CreateResult synthCreate(std::uint32_t max_chip_instances,
		std::uint32_t max_voices,
		const Environment* environment = nullptr) noexcept;

	/// Destroys a synth and frees its pools. Null-safe.
	/// @param synth The synth to destroy; may be null.
	/// @pre No other call on `synth` is in progress or follows.
	/// @post Every handle of `synth` is invalid. The `context` given to @ref synthSetMixer is no longer used.
	/// @threading Called from the control side. May block in the memory allocator.
	/// @ingroup synth
	void synthDestroy(SynthHandle synth) noexcept;

	/// Derives every rate-dependent value from `sample_rate_hz` and selects
	/// `oversampling_factor`. Invalidates every live instance.
	/// @param synth The synth to configure.
	/// @param sample_rate_hz The output sample rate, in hertz.
	/// @param oversampling_factor The internal oversampling factor, a positive integer. Oscillators, envelopes, and
	///   filter run at this multiple of `sample_rate_hz`, so CPU cost grows with it.
	/// @pre No other call on `synth` is in progress, including calls from the render side such as @ref synthRender.
	/// @post Every instance is expired, so @ref synthIsComplete reports true for it.
	/// @warning An oversampling factor above `default_oversampling_factor` can allocate; if allocation fails, the
	///   program terminates.
	/// @threading Called from the control side. May block in the memory allocator.
	/// @ingroup synth
	void synthConfigure(SynthHandle synth, std::uint32_t sample_rate_hz, std::uint32_t oversampling_factor) noexcept;

	/// Sets the grit scale.
	/// @param synth The synth to update.
	/// @param scale Grit scale in `[0, 1]`: 0 is full float resolution, 1 is each sound's authored grit profile.
	/// @threading Called from the control side.
	/// @ingroup synth
	void synthSetGritScale(SynthHandle synth, float scale) noexcept;

	/// Installs the mixer that @ref synthRender submits each instance's block to.
	/// A null `mixer`, or a struct with all four function pointers null, installs
	/// the default mixer. A partly filled struct is an error: it is logged as a
	/// warning, fails a debug assertion, and installs the default mixer.
	/// @param synth The synth to update.
	/// @param mixer The callbacks to install, or null for the default mixer. The pointed-to struct is copied.
	/// @pre No call to @ref synthRender is in progress.
	/// @threading Called from the control side. `mixer` is copied and need not outlive the call. The callbacks
	///   it names are called from the render side, and some getter callbacks, such as `get_channel_count`, also from
	///   the control side. Their lifetime and thread rules are in @ref MixerCallbacks.
	/// @relates MixerCallbacks
	/// @ingroup synth
	void synthSetMixer(SynthHandle synth, const MixerCallbacks* mixer) noexcept;

	/// Starts one instance playing `program`, seeded by `instance_seed`.
	/// Fails if the program is invalid, no instance slot is free, or too few voice
	/// slots are free; it never stops a running instance to make room. A released
	/// instance slot is free again only after the next @ref synthRender or @ref
	/// synthConfigure. Allocates no memory.
	/// @param synth The synth that supplies the instance.
	/// @param program The sound to play.
	/// @param instance_seed Seed for the instance's stochastic behavior.
	/// @param onset_delay_base_ticks 0 gates the voices at start; N gates them on the first block of the Nth base tick
	///   evaluated for the instance. A voice with a `start_subtick` activates at the later of the two.
	/// @param initial Initial external control; see @ref ControlParams.
	/// @return The started instance, or a falsy result stating why it could not start.
	/// @threading Called from the control side. `program` is borrowed, not copied: it must stay alive and
	///   unmodified until the first of @ref synthRelease for the returned instance, @ref synthConfigure, or @ref
	///   synthDestroy. @ref synthIsComplete returning true does not end the borrow. A failed start does not retain
	///   `program`. After @ref synthRelease the program may be destroyed or modified, even while @ref synthRender is
	///   running.
	/// @relates StartResult
	/// @ingroup synth
	[[nodiscard]] StartResult synthStart(SynthHandle synth,
		const SoundProgram& program,
		std::uint64_t instance_seed,
		std::uint32_t onset_delay_base_ticks = 0,
		ControlParams initial = {}) noexcept;

	/// Returns true when every voice of the instance has finished, and for an
	/// expired handle.
	/// @param synth The synth owning the instance.
	/// @param instance The instance to query.
	/// @return Whether the instance has finished or its handle is expired.
	/// @threading Called from the control side.
	/// @ingroup synth
	[[nodiscard]] bool synthIsComplete(SynthHandle synth, ChipInstanceHandle instance) noexcept;

	/// Returns the instance's slot to the pool. Valid in every state of a live
	/// instance, including before it is complete. A no-op for an expired handle.
	/// @param synth The synth owning the instance.
	/// @param instance The instance to release.
	/// @post `instance` is expired.
	/// @threading Called from the control side.
	/// @ingroup synth
	void synthRelease(SynthHandle synth, ChipInstanceHandle instance) noexcept;

	/// Holds or releases the gate of a sustained instance (`has_release_subtick`
	/// is false). No effect on a one-shot instance. No effect if `instance` is
	/// expired.
	/// @param synth The synth owning the instance.
	/// @param instance The instance to gate.
	/// @param gate_on True to hold the gate on, false to release it.
	/// @threading Called from the control side.
	/// @ingroup synth
	void synthSetGate(SynthHandle synth, ChipInstanceHandle instance, bool gate_on) noexcept;

	/// Revises the external pitch, cutoff, and gain control of an instance,
	/// applied at the next @ref synthEvaluateTick; see @ref ControlParams. Applies
	/// to sustained and one-shot instances alike. No effect if `instance` is
	/// expired.
	/// @param synth The synth owning the instance.
	/// @param instance The instance to update.
	/// @param control The control revision to apply.
	/// @threading Called from the control side.
	/// @relates ControlParams
	/// @ingroup synth
	void synthUpdateControlParams(SynthHandle synth, ChipInstanceHandle instance, ControlParams control) noexcept;

	/// Returns how many parameter blocks one base tick of this
	/// instance emits.
	/// @param synth The synth owning the instance.
	/// @param instance The instance to query.
	/// @return The number of blocks per base tick, or zero if `instance` is expired.
	/// @threading Called from the control side.
	/// @ingroup synth
	[[nodiscard]] std::uint32_t synthGetBlocksPerBaseTick(SynthHandle synth, ChipInstanceHandle instance) noexcept;

	/// Advances the instance by one base tick, writing the resulting
	/// blocks to `blocks` and returning the count written. When `sub_block_spans` is
	/// non-empty, its first `written` entries receive each block's sub-block
	/// span.
	/// @param synth The synth owning the instance.
	/// @param instance The instance to advance.
	/// @param blocks Receives the emitted parameter blocks.
	/// @param sub_block_spans Optionally receives each block's sub-block span.
	/// @return The number of blocks written to `blocks`, or zero if `instance` is expired.
	/// @pre @ref synthConfigure has been called at least once on `synth`. Before that, the call emits no blocks
	///   and returns zero.
	/// @pre `blocks` holds at least @ref synthGetBlocksPerBaseTick entries.
	/// @threading Called from the control side. `blocks` and `sub_block_spans` are written during the call and not
	///   retained. The consumer carries the blocks to the render
	///   side, where they go to @ref synthApplyBlock.
	/// @relates TickSubBlockSpan
	/// @ingroup synth
	[[nodiscard]] std::uint32_t synthEvaluateTick(SynthHandle synth,
		ChipInstanceHandle instance,
		shims::Span<ParameterBlock> blocks,
		shims::Span<TickSubBlockSpan> sub_block_spans = {}) noexcept;

	/// Installs `block` on the instance it targets (`block.target`). Ignored
	/// for an expired handle.
	/// @param synth The synth owning the targeted instance.
	/// @param block The parameters to install.
	/// @threading Called from the render side. `block` is copied during the call and not retained.
	/// @ingroup synth
	void synthApplyBlock(SynthHandle synth, const ParameterBlock& block) noexcept;

	/// Returns the interleaved channels the installed mixer writes per frame:
	/// 1 for the default mixer, else what the mixer's `get_channel_count`
	/// reports. Lets a consumer holding only a handle size the span it passes
	/// to @ref synthRender.
	/// @param synth The synth to query.
	/// @return Interleaved channels written per frame.
	/// @threading Called from the control side.
	/// @ingroup synth
	[[nodiscard]] std::uint32_t synthGetChannelCount(SynthHandle synth) noexcept;

	/// Renders every live instance and submits each to the installed mixer,
	/// which writes `output`. The consumer owns `output`; its previous
	/// contents are overwritten.
	///
	/// The call does not allocate memory, take a lock, make a system call, or log, so it is safe on a real-time
	/// audio thread provided the installed mixer's callbacks obey the same rules. On x86 targets, while it runs it
	/// puts the calling thread's floating-point unit in flush-to-zero, denormals-are-zero mode and restores the
	/// previous mode before it returns; on other targets it leaves the mode alone.
	/// @param synth The synth to render.
	/// @param output Caller-owned buffer receiving the mixed frames.
	/// @pre `output` holds a whole number of sub-blocks, each @ref sub_block_frame_count frames times
	///   @ref synthGetChannelCount channels.
	/// @threading Called from the render side. Runs the callbacks of the installed mixer (@ref synthSetMixer) on
	///   the calling thread. `output` is written during the call and not retained.
	/// @ingroup synth
	void synthRender(SynthHandle synth, shims::Span<float> output) noexcept;
}
