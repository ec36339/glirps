#include "glirps/glirps.hpp"

#include "impl/Allocator.hpp"
#include "impl/Logger.hpp"
#include "impl/Mixer.hpp"
#include "impl/SumToMonoMixer.hpp"
#include "impl/Synth.hpp"

#include <cassert>
#include <expected>
#include <memory>
#include <optional>
#include <span>

namespace glirps
{
	namespace
	{
		// Adapts the C-style `MixerCallbacks` to the backend `Mixer` contract.
		// Lives only in this translation unit.
		class CallbackMixer final : public impl::Mixer
		{
		public:
			explicit CallbackMixer(const MixerCallbacks& callbacks) noexcept
				: callbacks_(callbacks)
			{
			}

			[[nodiscard]] std::uint32_t getChannelCount() const noexcept override
			{
				return callbacks_.get_channel_count(callbacks_.context);
			}

			void beginBlock(std::span<float> output, std::uint32_t frame_count) noexcept override
			{
				callbacks_.begin_block(callbacks_.context, shims::toSpan(output), frame_count);
			}

			void submitInstance(ChipInstanceHandle instance, std::span<const float> mono) noexcept override
			{
				callbacks_.submit_instance(callbacks_.context, instance, shims::toSpan(mono));
			}

			void endBlock() noexcept override { callbacks_.end_block(callbacks_.context); }

		private:
			MixerCallbacks callbacks_;
		};

		// Reports an allocator pair with exactly one function pointer set, which
		// `impl::AllocatorRef` replaces with the default allocation.
		void warnOnPartialAllocator(const impl::Logger& logger, const AllocatorCallbacks& allocator) noexcept
		{
			if ((allocator.allocate == nullptr) != (allocator.deallocate == nullptr))
			{
				logger.log(LogSeverity::warning,
					"AllocatorCallbacks is partially populated; using the default allocation");
			}
		}

		// The name of `error` for a log message.
		[[nodiscard]] const char* getStartErrorName(StartError error) noexcept
		{
			switch (error)
			{
			case StartError::chip_instances_exhausted:
				return "chip_instances_exhausted";
			case StartError::voices_exhausted:
				return "voices_exhausted";
			case StartError::invalid_program:
				return "invalid_program";
			}
			return "unknown";
		}
	}
}

// The opaque type `SynthHandle` points at. Owns the backend `Synth` plus the
// two mixers it may reference, and the allocator and logger of the environment
// it was created with. The mixers are declared before `backend` so they
// outlive it: `Synth` stores a non-owning `Mixer&`, so the referenced mixer
// SHALL be destroyed after the synth.
struct glirps::SynthObject final
{
	SynthObject(std::uint32_t max_chip_instances,
		std::uint32_t max_voices,
		const impl::AllocatorRef& environment_allocator,
		const impl::Logger& environment_logger)
		: allocator(environment_allocator)
		, logger(environment_logger)
		, backend(max_chip_instances, max_voices, environment_allocator)
	{
	}

	// The allocation of the environment, used to release this object.
	impl::AllocatorRef allocator;
	// The log callback of the environment.
	impl::Logger logger;
	// Restores the built-in mono mixer, used for a null or partially-null
	// callback struct.
	impl::SumToMonoMixer default_mixer;
	// The active callback adaptor, present only while custom callbacks are
	// installed.
	std::optional<CallbackMixer> callback_mixer;
	// The synthesis backend behind the opaque handle.
	impl::Synth backend;
};

namespace glirps
{
	// NOLINTBEGIN(cppcoreguidelines-owning-memory): `SynthHandle` is a raw
	// owning pointer by design (a binary-compatible C-style boundary
	// with no RAII types on the surface, per glirps.hpp); `synthCreate` and
	// `synthDestroy` are its matched allocate/free pair.
	CreateResult
	synthCreate(std::uint32_t max_chip_instances, std::uint32_t max_voices, const Environment* environment) noexcept
	{
		const Environment resolved = environment != nullptr ? *environment : Environment{};
		const impl::Logger logger(resolved.logger);
		warnOnPartialAllocator(logger, resolved.allocator);
		// A synth that could never start an instance is an invalid request.
		if (max_chip_instances == 0 || max_voices == 0)
		{
			logger.log(LogSeverity::error,
				"synthCreate: max_chip_instances ({}) and max_voices ({}) SHALL be non-zero",
				max_chip_instances,
				max_voices);
			return CreateResult{.error = CreateError::invalid_arguments};
		}
		// The backend constructor allocates and may throw `std::bad_alloc`;
		// catch everything so the `noexcept` boundary reports failure instead.
		try
		{
			const impl::AllocatorRef allocator(resolved.allocator);
			impl::UniquePtr<SynthObject> object
				= impl::makeUnique<SynthObject>(allocator, max_chip_instances, max_voices, allocator, logger);
			logger.log(LogSeverity::debug,
				"synthCreate: created a synth with {} chip instances and {} voices",
				max_chip_instances,
				max_voices);
			return CreateResult{.synth = object.release()};
		}
		catch (...)
		{
			logger.log(LogSeverity::error, "synthCreate: allocation failed");
			return CreateResult{.error = CreateError::allocation_failure};
		}
	}

	void synthDestroy(SynthHandle synth) noexcept
	{
		if (synth == nullptr)
			return;
		// The object holds the allocator that releases it, so copy both out first.
		const impl::AllocatorRef allocator = synth->allocator;
		const impl::Logger logger = synth->logger;
		logger.log(LogSeverity::debug, "synthDestroy: destroying a synth");
		impl::destroyAndDeallocate(synth, allocator);
	}
	// NOLINTEND(cppcoreguidelines-owning-memory)

	void synthConfigure(SynthHandle synth, std::uint32_t sample_rate_hz, std::uint32_t oversampling_factor) noexcept
	{
		synth->logger.log(LogSeverity::debug,
			"synthConfigure: sample rate {} Hz, oversampling factor {}",
			sample_rate_hz,
			oversampling_factor);
		synth->backend.configure(sample_rate_hz, oversampling_factor);
	}

	void synthSetGritScale(SynthHandle synth, float scale) noexcept { synth->backend.setGritScale(scale); }

	void synthSetMixer(SynthHandle synth, const MixerCallbacks* mixer) noexcept
	{
		const bool complete = mixer != nullptr && mixer->get_channel_count != nullptr && mixer->begin_block != nullptr
			&& mixer->submit_instance != nullptr && mixer->end_block != nullptr;
		if (!complete)
		{
			// A partially-populated struct is an authoring error: report it,
			// flag it in debug, then fall back to the default mono mixer in
			// every build.
			const bool any_set = mixer != nullptr
				&& (mixer->get_channel_count != nullptr || mixer->begin_block != nullptr
					|| mixer->submit_instance != nullptr || mixer->end_block != nullptr);
			if (any_set)
			{
				synth->logger.log(LogSeverity::warning,
					"synthSetMixer: MixerCallbacks is partially populated; using the default mixer");
			}
			assert((mixer == nullptr
					   || (mixer->get_channel_count == nullptr && mixer->begin_block == nullptr
						   && mixer->submit_instance == nullptr && mixer->end_block == nullptr))
				&& "MixerCallbacks must have all four function pointers set or none");
			synth->callback_mixer.reset();
			synth->backend.setMixer(synth->default_mixer);
			return;
		}
		synth->callback_mixer.emplace(*mixer);
		synth->backend.setMixer(*synth->callback_mixer);
	}

	StartResult synthStart(SynthHandle synth,
		const SoundProgram& program,
		std::uint64_t instance_seed,
		std::uint32_t onset_delay_base_ticks,
		ControlParams initial) noexcept
	{
		const std::expected<ChipInstanceHandle, StartError> result
			= synth->backend.start(program, instance_seed, onset_delay_base_ticks, initial);
		if (result.has_value())
			return StartResult{.instance = *result};
		// Pool exhaustion is an expected, recoverable condition that a consumer
		// handles by evicting or retrying, so only a rejected program warns.
		const LogSeverity severity
			= result.error() == StartError::invalid_program ? LogSeverity::warning : LogSeverity::debug;
		synth->logger.log(severity, "synthStart: failed with {}", getStartErrorName(result.error()));
		return StartResult{.error = result.error()};
	}

	bool synthIsComplete(SynthHandle synth, ChipInstanceHandle instance) noexcept
	{
		return synth->backend.isComplete(instance);
	}

	void synthRelease(SynthHandle synth, ChipInstanceHandle instance) noexcept { synth->backend.release(instance); }

	void synthSetGate(SynthHandle synth, ChipInstanceHandle instance, bool gate_on) noexcept
	{
		synth->backend.setGate(instance, gate_on);
	}

	void synthUpdateControlParams(SynthHandle synth, ChipInstanceHandle instance, ControlParams control) noexcept
	{
		synth->backend.updateControlParams(instance, control);
	}

	std::uint32_t synthGetBlocksPerBaseTick(SynthHandle synth, ChipInstanceHandle instance) noexcept
	{
		return synth->backend.getBlocksPerBaseTick(instance);
	}

	std::uint32_t synthEvaluateTick(SynthHandle synth,
		ChipInstanceHandle instance,
		shims::Span<ParameterBlock> blocks,
		shims::Span<TickSubBlockSpan> sub_block_spans) noexcept
	{
		return synth->backend.evaluateTick(instance,
			static_cast<std::span<ParameterBlock>>(blocks),
			static_cast<std::span<TickSubBlockSpan>>(sub_block_spans));
	}

	void synthApplyBlock(SynthHandle synth, const ParameterBlock& block) noexcept { synth->backend.applyBlock(block); }

	std::uint32_t synthGetChannelCount(SynthHandle synth) noexcept { return synth->backend.getChannelCount(); }

	void synthRender(SynthHandle synth, shims::Span<float> output) noexcept
	{
		synth->backend.render(static_cast<std::span<float>>(output));
	}
}
