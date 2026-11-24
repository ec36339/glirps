#include "impl/BlockRenderer.hpp"

#include "glirps/ParameterBlock.hpp"
#include "glirps/constants.hpp"
#include "impl/ChipInstance.hpp"
#include "impl/Envelope.hpp"

#include <array>
#include <cassert>
#include <memory>
#include <span>

namespace glirps::impl
{
	namespace
	{
		// A fixed non-zero seed for voices that never get an explicit seed,
		// so their noise register is never stuck at zero.
		constexpr std::uint64_t default_voice_seed = 0x5a5au;
	}

	struct BlockRenderer::Impl
	{
		ChipInstance chip;

		explicit Impl(std::uint32_t sample_rate_hz, std::uint32_t oversampling_factor, const AllocatorRef& allocator)
			: chip(sample_rate_hz, max_voices_per_instance, default_voice_seed, oversampling_factor, allocator)
		{
		}
	};

	BlockRenderer::BlockRenderer(std::uint32_t sample_rate_hz,
		std::uint32_t oversampling_factor,
		const AllocatorRef& allocator)
		: impl_(makeUnique<Impl>(allocator, sample_rate_hz, oversampling_factor, allocator))
	{
	}

	BlockRenderer::~BlockRenderer() = default;

	void BlockRenderer::reconfigure(std::uint32_t sample_rate_hz, std::uint32_t oversampling_factor) noexcept
	{
		impl_->chip.reconfigure(sample_rate_hz, oversampling_factor);
	}

	void BlockRenderer::resetState() noexcept { impl_->chip.resetState(); }

	void BlockRenderer::seedVoiceNoise(std::uint32_t voice_index, std::uint32_t seed) noexcept
	{
		impl_->chip.seedVoiceNoise(voice_index, seed);
	}

	void BlockRenderer::configureVoice(std::uint32_t voice_index,
		Waveform waveform,
		float frequency_hz,
		float pulse_width,
		float gain,
		std::uint64_t seed) noexcept
	{
		impl_->chip.setVoice(voice_index, waveform, frequency_hz, pulse_width, gain, seed);
	}

	void BlockRenderer::setVoiceEnvelope(std::uint32_t voice_index,
		std::uint32_t attack_index,
		std::uint32_t decay_index,
		std::uint32_t release_index,
		float sustain_level) noexcept
	{
		impl_->chip.setVoiceEnvelope(voice_index, attack_index, decay_index, release_index, sustain_level);
	}

	// Selects one voice's envelope from durations in seconds and a sustain
	// level. Used by `Synth::start` to configure the instance's envelopes from
	// the program once: authored seconds become per-sample rates once at
	// `start`, never on the render path.
	void BlockRenderer::setVoiceEnvelopeSeconds(std::uint32_t voice_index,
		float attack_seconds,
		float decay_seconds,
		float release_seconds,
		float sustain_level) noexcept
	{
		impl_->chip.setVoiceEnvelopeSeconds(voice_index, attack_seconds, decay_seconds, release_seconds, sustain_level);
	}

	void BlockRenderer::setGate(std::uint32_t voice_index, bool gate_on) noexcept
	{
		impl_->chip.setGate(voice_index, gate_on);
	}

	void BlockRenderer::setVoiceFrequency(std::uint32_t voice_index, float frequency_hz) noexcept
	{
		impl_->chip.setVoiceFrequency(voice_index, frequency_hz);
	}

	void BlockRenderer::setRingModSource(std::uint32_t voice_index, std::uint32_t source_index) noexcept
	{
		impl_->chip.setRingModSource(voice_index, source_index);
	}

	void BlockRenderer::setHardSyncSource(std::uint32_t voice_index, std::uint32_t source_index) noexcept
	{
		impl_->chip.setHardSyncSource(voice_index, source_index);
	}

	void BlockRenderer::setFilter(float cutoff_hz, float resonance_q, FilterMode mode, float saturation) noexcept
	{
		// Build the set with every boolean explicit: the `FilterModeSet`
		// default `lowpass = true` would otherwise leak lowpass into every
		// non-lowpass selection.
		FilterModeSet modes{.lowpass = false, .bandpass = false, .highpass = false, .notch = false};
		switch (mode)
		{
		case FilterMode::lowpass:
			modes.lowpass = true;
			break;
		case FilterMode::bandpass:
			modes.bandpass = true;
			break;
		case FilterMode::highpass:
			modes.highpass = true;
			break;
		case FilterMode::notch:
			modes.notch = true;
			break;
		}
		impl_->chip.setFilter(cutoff_hz, resonance_q, modes, saturation);
	}

	void BlockRenderer::setFilterCutoff(float cutoff_hz) noexcept { impl_->chip.setFilterCutoff(cutoff_hz); }

	void BlockRenderer::setOutputGain(float gain) noexcept { impl_->chip.setOutputGain(gain); }

	std::uint32_t BlockRenderer::getDecimatorGroupDelayInFrames() const noexcept
	{
		return impl_->chip.getDecimatorGroupDelayInFrames();
	}

	void BlockRenderer::render(std::span<float> output) noexcept
	{
		assert(output.size() % sub_block_frame_count == 0);
		for (float& out : output)
			out = impl_->chip.advance();
	}

	float BlockRenderer::renderOne() noexcept { return impl_->chip.advance(); }

	void BlockRenderer::applyBlock(const ParameterBlock& block) noexcept
	{
		std::uint32_t i = 0;
		for (const VoiceParameters& voice : std::span<const VoiceParameters>(block.voices.data(), block.voice_count))
		{
			impl_->chip.setVoiceParameters(i, voice.waveform, voice.frequency_hz, voice.pulse_width, voice.gain);
			impl_->chip.setGate(i, voice.gate_on);
			++i;
		}
		impl_->chip.applyFilterBlock(block.cutoff_hz, block.resonance_q, block.filter_modes);
		impl_->chip.applyOutputGain(block.output_gain);
	}

	void BlockRenderer::applyBaseBlock(const ParameterBlock& block) noexcept
	{
		std::uint32_t i = 0;
		for (const VoiceParameters& voice : std::span<const VoiceParameters>(block.voices.data(), block.voice_count))
		{
			impl_->chip.resetVoiceParameters(i, voice.waveform, voice.frequency_hz, voice.pulse_width, voice.gain);
			impl_->chip.setGate(i, voice.gate_on);
			++i;
		}
		impl_->chip.resetFilterBlock(block.cutoff_hz, block.resonance_q, block.filter_modes);
		impl_->chip.resetOutputGain(block.output_gain);
	}

	void BlockRenderer::setFilterSaturation(float saturation) noexcept { impl_->chip.setFilterSaturation(saturation); }

	bool BlockRenderer::isComplete() const noexcept { return impl_->chip.isComplete(); }
}
