#include "glirps/glirps_render.hpp"

#include "impl/Allocator.hpp"
#include "impl/BlockRenderer.hpp"
#include "impl/Logger.hpp"

// The opaque type `BlockRendererHandle` points at. Owns the backend
// `BlockRenderer` plus the allocator and logger of the environment it was
// created with.
struct glirps::BlockRendererObject final
{
	BlockRendererObject(std::uint32_t sample_rate_hz,
		std::uint32_t oversampling_factor,
		const impl::AllocatorRef& environment_allocator,
		const impl::Logger& environment_logger)
		: allocator(environment_allocator)
		, logger(environment_logger)
		, backend(sample_rate_hz, oversampling_factor, environment_allocator)
	{
	}

	// The allocation of the environment, used to release this object.
	impl::AllocatorRef allocator;
	// The log callback of the environment.
	impl::Logger logger;
	// The synthesis backend behind the opaque handle.
	impl::BlockRenderer backend;
};

namespace glirps
{
	// NOLINTBEGIN(cppcoreguidelines-owning-memory): `BlockRendererHandle` is a
	// raw owning pointer by design (a binary-compatible C-style
	// boundary with no RAII types on the surface, per glirps_render.hpp);
	// `blockRendererCreate` and `blockRendererDestroy` are its matched
	// allocate/free pair.
	BlockRendererCreateResult blockRendererCreate(std::uint32_t sample_rate_hz,
		std::uint32_t oversampling_factor,
		const Environment* environment) noexcept
	{
		const Environment resolved = environment != nullptr ? *environment : Environment{};
		const impl::Logger logger(resolved.logger);
		if ((resolved.allocator.allocate == nullptr) != (resolved.allocator.deallocate == nullptr))
		{
			logger.log(LogSeverity::warning, "AllocatorCallbacks is partially populated; using the default allocation");
		}
		// The wrapped constructor allocates and may throw `std::bad_alloc`;
		// catch everything so the `noexcept` boundary reports failure instead.
		try
		{
			const impl::AllocatorRef allocator(resolved.allocator);
			impl::UniquePtr<BlockRendererObject> object = impl::makeUnique<BlockRendererObject>(allocator,
				sample_rate_hz,
				oversampling_factor,
				allocator,
				logger);
			logger.log(LogSeverity::debug,
				"blockRendererCreate: created a renderer at {} Hz, oversampling factor {}",
				sample_rate_hz,
				oversampling_factor);
			return BlockRendererCreateResult{.renderer = object.release()};
		}
		catch (...)
		{
			logger.log(LogSeverity::error, "blockRendererCreate: allocation failed");
			return BlockRendererCreateResult{.error = BlockRendererCreateError::allocation_failure};
		}
	}

	void blockRendererDestroy(BlockRendererHandle renderer) noexcept
	{
		if (renderer == nullptr)
			return;
		// The object holds the allocator that releases it, so copy both out first.
		const impl::AllocatorRef allocator = renderer->allocator;
		const impl::Logger logger = renderer->logger;
		logger.log(LogSeverity::debug, "blockRendererDestroy: destroying a renderer");
		impl::destroyAndDeallocate(renderer, allocator);
	}
	// NOLINTEND(cppcoreguidelines-owning-memory)

	void blockRendererReconfigure(BlockRendererHandle renderer,
		std::uint32_t sample_rate_hz,
		std::uint32_t oversampling_factor) noexcept
	{
		renderer->logger.log(LogSeverity::debug,
			"blockRendererReconfigure: sample rate {} Hz, oversampling factor {}",
			sample_rate_hz,
			oversampling_factor);
		renderer->backend.reconfigure(sample_rate_hz, oversampling_factor);
	}

	void blockRendererResetState(BlockRendererHandle renderer) noexcept { renderer->backend.resetState(); }

	void
	blockRendererSeedVoiceNoise(BlockRendererHandle renderer, std::uint32_t voice_index, std::uint32_t seed) noexcept
	{
		renderer->backend.seedVoiceNoise(voice_index, seed);
	}

	void blockRendererConfigureVoice(BlockRendererHandle renderer,
		std::uint32_t voice_index,
		Waveform waveform,
		float frequency_hz,
		float pulse_width,
		float gain,
		std::uint64_t seed) noexcept
	{
		renderer->backend.configureVoice(voice_index, waveform, frequency_hz, pulse_width, gain, seed);
	}

	void blockRendererSetVoiceEnvelope(BlockRendererHandle renderer,
		std::uint32_t voice_index,
		std::uint32_t attack_index,
		std::uint32_t decay_index,
		std::uint32_t release_index,
		float sustain_level) noexcept
	{
		renderer->backend.setVoiceEnvelope(voice_index, attack_index, decay_index, release_index, sustain_level);
	}

	void blockRendererSetVoiceEnvelopeSeconds(BlockRendererHandle renderer,
		std::uint32_t voice_index,
		float attack_seconds,
		float decay_seconds,
		float release_seconds,
		float sustain_level) noexcept
	{
		renderer->backend.setVoiceEnvelopeSeconds(voice_index,
			attack_seconds,
			decay_seconds,
			release_seconds,
			sustain_level);
	}

	void blockRendererSetGate(BlockRendererHandle renderer, std::uint32_t voice_index, bool gate_on) noexcept
	{
		renderer->backend.setGate(voice_index, gate_on);
	}

	void
	blockRendererSetVoiceFrequency(BlockRendererHandle renderer, std::uint32_t voice_index, float frequency_hz) noexcept
	{
		renderer->backend.setVoiceFrequency(voice_index, frequency_hz);
	}

	void blockRendererSetRingModSource(BlockRendererHandle renderer,
		std::uint32_t voice_index,
		std::uint32_t source_index) noexcept
	{
		renderer->backend.setRingModSource(voice_index, source_index);
	}

	void blockRendererSetHardSyncSource(BlockRendererHandle renderer,
		std::uint32_t voice_index,
		std::uint32_t source_index) noexcept
	{
		renderer->backend.setHardSyncSource(voice_index, source_index);
	}

	void blockRendererSetFilter(BlockRendererHandle renderer,
		float cutoff_hz,
		float resonance_q,
		FilterMode mode,
		float saturation) noexcept
	{
		renderer->backend.setFilter(cutoff_hz, resonance_q, mode, saturation);
	}

	void blockRendererSetFilterCutoff(BlockRendererHandle renderer, float cutoff_hz) noexcept
	{
		renderer->backend.setFilterCutoff(cutoff_hz);
	}

	void blockRendererSetFilterSaturation(BlockRendererHandle renderer, float saturation) noexcept
	{
		renderer->backend.setFilterSaturation(saturation);
	}

	void blockRendererSetOutputGain(BlockRendererHandle renderer, float gain) noexcept
	{
		renderer->backend.setOutputGain(gain);
	}

	std::uint32_t blockRendererGetDecimatorGroupDelayInFrames(BlockRendererHandle renderer) noexcept
	{
		return renderer->backend.getDecimatorGroupDelayInFrames();
	}

	void blockRendererApplyBlock(BlockRendererHandle renderer, const ParameterBlock& block) noexcept
	{
		renderer->backend.applyBlock(block);
	}

	void blockRendererApplyBaseBlock(BlockRendererHandle renderer, const ParameterBlock& block) noexcept
	{
		renderer->backend.applyBaseBlock(block);
	}

	void blockRendererRender(BlockRendererHandle renderer, shims::Span<float> output) noexcept
	{
		renderer->backend.render(static_cast<std::span<float>>(output));
	}

	float blockRendererRenderOne(BlockRendererHandle renderer) noexcept { return renderer->backend.renderOne(); }

	bool blockRendererIsComplete(BlockRendererHandle renderer) noexcept { return renderer->backend.isComplete(); }
}
