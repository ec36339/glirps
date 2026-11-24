#pragma once

#include "Allocator.hpp"
#include "BlockRenderer.hpp"
#include "Decimator.hpp"
#include "Envelope.hpp"
#include "LfsrNoise.hpp"
#include "Oscillator.hpp"
#include "StateVariableFilter.hpp"
#include "glirps/Waveform.hpp"
#include "glirps/constants.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace glirps::impl
{
	// Test-only accessor for `ChipInstance`'s smoothed ramp state, defined in
	// the test target rather than in `glirps`; `ChipInstance` only befriends
	// it, so production code gains no accessor. The friend declaration below
	// is the only production footprint.
	struct ChipInstanceTestAccess;

	// The 64-bit seed is truncated to the LFSR's 32-bit input width.
	inline constexpr std::uint64_t seed_truncation_mask = std::numeric_limits<std::uint32_t>::max();

	// Base of the exponent converting a decibel target to a linear factor:
	// linear = dbfs_to_linear_base^(dbfs / decibel_amplitude_scale), the
	// inverse of level_dbfs = decibel_amplitude_scale * log10(linear).
	inline constexpr float dbfs_to_linear_base = 10.0f;

	// Default shared filter parameters: full-bandwidth cutoff and the
	// Butterworth Q.
	inline constexpr float default_filter_cutoff_hz = 20000.0f;
	inline constexpr float default_filter_resonance_q = 0.707f;

	// One voice of a chip instance: oscillator, its pitched noise register,
	// and its envelope, plus the parameters that shape the render.
	struct ChipVoice
	{
		Oscillator oscillator;
		LfsrNoise noise;
		Envelope envelope;
		Waveform waveform = Waveform::triangle;
		float pulse_width = 0.5f;
		// Per-voice gain is ramped over one sub-block to avoid zipper
		// noise: `gain_current` is what the render path reads, stepped
		// toward `gain_target` by `gain_increment` each output sample.
		float gain_target = 1.0f;
		float gain_current = 1.0f;
		float gain_increment = 0.0f;
		bool active = false;
		std::uint32_t ring_mod_source = invalid_voice;
		std::uint32_t hard_sync_source = invalid_voice;

		static constexpr std::uint32_t invalid_voice = static_cast<std::uint32_t>(-1);
	};

	// A chip instance: up to `max_voices_per_instance` voices, one shared
	// filter, and one shared output gain. The voice count is fixed at
	// construction.
	// Voices generate at `sample_rate_hz * oversampling_factor` and are
	// decimated back to the output rate.
	class ChipInstance
	{
	public:
		// Constructs a chip instance with `voice_count` voices, one shared
		// filter, and one shared output gain. Voices generate at
		// `sample_rate_hz * oversampling_factor` and are seeded from `seed`. The
		// decimator buffers allocate through `allocator`, so a failed
		// allocation throws `std::bad_alloc`.
		ChipInstance(std::uint32_t sample_rate_hz,
			std::uint32_t voice_count,
			std::uint64_t seed,
			std::uint32_t oversampling_factor = default_oversampling_factor,
			const AllocatorRef& allocator = AllocatorRef())
			: sample_rate_hz_(sample_rate_hz)
			, voice_count_(voice_count)
			, oversampling_factor_(oversampling_factor)
			, nominal_level_linear_(std::pow(dbfs_to_linear_base, nominal_source_level_db / decibel_amplitude_scale))
			, filter_(sample_rate_hz * oversampling_factor)
			, decimator_(sample_rate_hz, oversampling_factor, allocator)
		{
			for (std::uint32_t i = 0; i < max_voices_per_instance; ++i)
			{
				voices_.at(i).oscillator = Oscillator(sample_rate_hz, oversampling_factor);
				voices_.at(i).noise = LfsrNoise(static_cast<std::uint32_t>(seed & seed_truncation_mask));
				voices_.at(i).envelope = Envelope(sample_rate_hz * oversampling_factor);
				voices_.at(i).envelope.setRates(0, 0, 0, 1.0f);
				voices_.at(i).envelope.setGate(true);
			}
		}

		// Recomputes every rate-dependent constant for `sample_rate_hz` and
		// `oversampling_factor`. This also resets render state (oscillator
		// phase, envelope level, gate), so it is not safe to call on a slot
		// whose render state must survive the call.
		void reconfigure(std::uint32_t sample_rate_hz, std::uint32_t oversampling_factor) noexcept
		{
			sample_rate_hz_ = sample_rate_hz;
			oversampling_factor_ = oversampling_factor;
			const std::uint32_t generation_rate_hz = sample_rate_hz * oversampling_factor;
			filter_ = StateVariableFilter(generation_rate_hz);
			decimator_.reconfigure(sample_rate_hz, oversampling_factor);
			for (ChipVoice& voice : voices_)
			{
				voice.oscillator = Oscillator(sample_rate_hz, oversampling_factor);
				voice.envelope = Envelope(generation_rate_hz);
			}
		}

		// Configures one voice's base parameters and seeds its noise register.
		// Snaps gain directly with no ramp: this is direct configuration,
		// not a block-driven transition.
		void setVoice(std::uint32_t voice_index,
			Waveform waveform,
			float frequency_hz,
			float pulse_width,
			float gain,
			std::uint64_t seed) noexcept
		{
			ChipVoice& voice = voices_.at(voice_index);
			voice.waveform = waveform;
			voice.oscillator.setFrequency(frequency_hz);
			voice.oscillator.setPulseWidth(pulse_width);
			voice.pulse_width = pulse_width;
			voice.gain_current = voice.gain_target = gain;
			voice.gain_increment = 0.0f;
			voice.noise = LfsrNoise(static_cast<std::uint32_t>(seed & seed_truncation_mask));
			voice.active = true;
		}

		// Reseeds one voice's noise register from `seed`, leaving every
		// other voice field untouched. Used by `Synth::start` to seed every
		// voice of a reused slot's renderer from the instance's own
		// `instance_seed`, so a given seed always reproduces the same sound.
		void seedVoiceNoise(std::uint32_t voice_index, std::uint32_t seed) noexcept
		{
			voices_.at(voice_index).noise = LfsrNoise(seed);
		}

		// Selects one voice's envelope rate indices and sustain level.
		void setVoiceEnvelope(std::uint32_t voice_index,
			std::uint32_t attack_index,
			std::uint32_t decay_index,
			std::uint32_t release_index,
			float sustain_level) noexcept
		{
			voices_.at(voice_index).envelope.setRates(attack_index, decay_index, release_index, sustain_level);
		}

		// Selects one voice's envelope from durations in seconds and a sustain
		// level. Converted to per-sample rates once here, at the rate the chip
		// was constructed at.
		void setVoiceEnvelopeSeconds(std::uint32_t voice_index,
			float attack_seconds,
			float decay_seconds,
			float release_seconds,
			float sustain_level) noexcept
		{
			voices_.at(voice_index)
				.envelope.setRatesInSeconds(attack_seconds, decay_seconds, release_seconds, sustain_level);
		}

		// Sets one voice's oscillator frequency (used for sweeps).
		void setVoiceFrequency(std::uint32_t voice_index, float frequency_hz) noexcept
		{
			voices_.at(voice_index).oscillator.setFrequency(frequency_hz);
		}

		// Applies one voice's parameters from a parameter block, without
		// reseeding its noise register. The seed is fixed at `start`, so the
		// instance stays deterministic. Waveform and frequency step
		// immediately, since smoothing pitch would destroy the stepped
		// arpeggio character; pulse width is not smoothed either and steps
		// immediately too; gain is ramped to `gain` over one sub-block to
		// avoid zipper noise.
		void setVoiceParameters(std::uint32_t voice_index,
			Waveform waveform,
			float frequency_hz,
			float pulse_width,
			float gain) noexcept
		{
			ChipVoice& voice = voices_.at(voice_index);
			voice.waveform = waveform;
			voice.oscillator.setFrequency(frequency_hz);
			voice.oscillator.setPulseWidth(pulse_width);
			voice.pulse_width = pulse_width;
			voice.gain_target = gain;
			voice.gain_increment = (voice.gain_target - voice.gain_current) / static_cast<float>(sub_block_frame_count);
			voice.active = true;
		}

		// Snaps one voice's parameters directly to the given values with no
		// ramp on gain, so the first render does not ramp in from whatever a
		// reused slot last held. Waveform, frequency, and pulse width are never
		// ramped, so this differs from `setVoiceParameters` only in the gain
		// path.
		void resetVoiceParameters(std::uint32_t voice_index,
			Waveform waveform,
			float frequency_hz,
			float pulse_width,
			float gain) noexcept
		{
			ChipVoice& voice = voices_.at(voice_index);
			voice.waveform = waveform;
			voice.oscillator.setFrequency(frequency_hz);
			voice.oscillator.setPulseWidth(pulse_width);
			voice.pulse_width = pulse_width;
			voice.gain_current = voice.gain_target = gain;
			voice.gain_increment = 0.0f;
			voice.active = true;
		}

		// Returns true once every active voice's envelope has finished.
		[[nodiscard]] bool isComplete() const noexcept
		{
			for (std::uint32_t i = 0; i < voice_count_; ++i)
			{
				if (voices_.at(i).active && !voices_.at(i).envelope.isComplete())
					return false;
			}
			return true;
		}

		// Gates one voice's envelope on or off.
		void setGate(std::uint32_t voice_index, bool gate_on) noexcept
		{
			voices_.at(voice_index).envelope.setGate(gate_on);
		}

		// Enables ring modulation on `voice_index`: its output for its selected
		// waveform is multiplied by the `source_index` voice's output for that
		// voice's own selected waveform.
		void setRingModSource(std::uint32_t voice_index, std::uint32_t source_index) noexcept
		{
			voices_.at(voice_index).ring_mod_source = source_index;
		}

		// Enables hard sync on `voice_index`: its oscillator phase is reset by
		// the `source_index` voice's oscillator overflow.
		void setHardSyncSource(std::uint32_t voice_index, std::uint32_t source_index) noexcept
		{
			voices_.at(voice_index).hard_sync_source = source_index;
		}

		// Configures the shared filter. Snaps cutoff and resonance directly
		// with no ramp: this is direct configuration, for a caller that steps
		// every sample itself and does not want the block-driven ramp. `advance`
		// passes the current values to the filter every sample, so no
		// recompute is needed here.
		void setFilter(float cutoff_hz, float resonance_q, FilterModeSet modes, float saturation) noexcept
		{
			cutoff_current_ = cutoff_target_ = cutoff_hz;
			resonance_current_ = resonance_target_ = resonance_q;
			cutoff_increment_ = 0.0f;
			resonance_increment_ = 0.0f;
			modes_ = modes;
			saturation_ = saturation;
		}

		// Updates only the shared filter cutoff (used for sweeps), with no
		// ramp.
		void setFilterCutoff(float cutoff_hz) noexcept
		{
			cutoff_current_ = cutoff_target_ = cutoff_hz;
			cutoff_increment_ = 0.0f;
		}

		// Sets the shared output gain, with no ramp.
		void setOutputGain(float gain) noexcept
		{
			output_gain_current_ = output_gain_target_ = gain;
			output_gain_increment_ = 0.0f;
		}

		// Sets the shared filter's cutoff, resonance, and modes from a
		// parameter block: modes step immediately, since only cutoff,
		// resonance, and gain are smoothed; cutoff and resonance ramp to their
		// targets over one sub-block to avoid zipper noise.
		// Saturation is not a per-tick field of `ParameterBlock` and is
		// untouched here; `setFilterSaturation` is the instance-lifetime entry
		// point for it.
		void applyFilterBlock(float cutoff_hz, float resonance_q, FilterModeSet modes) noexcept
		{
			modes_ = modes;
			cutoff_target_ = cutoff_hz;
			cutoff_increment_ = (cutoff_target_ - cutoff_current_) / static_cast<float>(sub_block_frame_count);
			resonance_target_ = resonance_q;
			resonance_increment_ = (resonance_target_ - resonance_current_) / static_cast<float>(sub_block_frame_count);
		}

		// Snaps the shared filter's cutoff, resonance, and modes directly with
		// no ramp, so the first render does not ramp in from whatever a reused
		// slot last held.
		void resetFilterBlock(float cutoff_hz, float resonance_q, FilterModeSet modes) noexcept
		{
			modes_ = modes;
			cutoff_current_ = cutoff_target_ = cutoff_hz;
			resonance_current_ = resonance_target_ = resonance_q;
			cutoff_increment_ = 0.0f;
			resonance_increment_ = 0.0f;
		}

		// Sets the shared filter's saturation. Fixed for the instance's whole
		// life: `ParameterBlock` carries no per-tick saturation field, so this
		// is set once from the program at `start` rather than per block.
		void setFilterSaturation(float saturation) noexcept { saturation_ = saturation; }

		// Sets the shared output gain from a parameter block, ramping to
		// `gain` over one sub-block to avoid zipper noise.
		void applyOutputGain(float gain) noexcept
		{
			output_gain_target_ = gain;
			output_gain_increment_
				= (output_gain_target_ - output_gain_current_) / static_cast<float>(sub_block_frame_count);
		}

		// Snaps the shared output gain directly to `gain` with no ramp, so
		// the first render does not ramp in from whatever a reused slot last
		// held.
		void resetOutputGain(float gain) noexcept
		{
			output_gain_current_ = output_gain_target_ = gain;
			output_gain_increment_ = 0.0f;
		}

		// Returns the voice count fixed at construction.
		[[nodiscard]] std::uint32_t getVoiceCount() const noexcept { return voice_count_; }

		// The decimator's group delay in output frames, so renders at different
		// oversampling factors can be aligned.
		[[nodiscard]] std::uint32_t getDecimatorGroupDelayInFrames() const noexcept
		{
			return decimator_.getGroupDelayInFrames();
		}

		// Advances one output sample: steps the cutoff, resonance, output gain,
		// and per-voice gain ramps once, then runs `oversampling_factor`
		// internal steps through the voices and filter, then takes the
		// decimated sample. Cutoff and resonance step once per output sample
		// rather than per oversampled sub-step, so a ramp completes over
		// `sub_block_frame_count` output samples regardless of the
		// oversampling factor.
		float advance() noexcept
		{
			stepRamp(cutoff_current_, cutoff_target_, cutoff_increment_);
			stepRamp(resonance_current_, resonance_target_, resonance_increment_);
			stepRamp(output_gain_current_, output_gain_target_, output_gain_increment_);
			for (std::uint32_t i = 0; i < voice_count_; ++i)
			{
				ChipVoice& voice = voices_.at(i);
				stepRamp(voice.gain_current, voice.gain_target, voice.gain_increment);
			}
			filter_.setParameters(cutoff_current_, resonance_current_, modes_, saturation_);

			float output = 0.0f;
			for (std::uint32_t sub = 0; sub < oversampling_factor_; ++sub)
			{
				// Advance every active oscillator first, so hard sync reads the
				// source's overflow before any synced voice resets.
				for (std::uint32_t i = 0; i < voice_count_; ++i)
				{
					ChipVoice& voice = voices_.at(i);
					if (!voice.active)
						continue;
					if (voice.oscillator.advance())
						voice.noise.shift();
				}
				for (std::uint32_t i = 0; i < voice_count_; ++i)
				{
					ChipVoice& voice = voices_.at(i);
					if (!voice.active || voice.hard_sync_source == ChipVoice::invalid_voice)
						continue;
					if (voices_.at(voice.hard_sync_source).oscillator.didOverflow())
						voice.oscillator.resetPhase();
				}

				float sum = 0.0f;
				for (std::uint32_t i = 0; i < voice_count_; ++i)
				{
					ChipVoice& voice = voices_.at(i);
					if (!voice.active)
						continue;
					float sample = oscillatorOutput(voice);
					if (voice.ring_mod_source != ChipVoice::invalid_voice)
						sample *= oscillatorOutput(voices_.at(voice.ring_mod_source));
					const float envelope_level = voice.envelope.advance();
					sum += sample * envelope_level * voice.gain_current;
				}

				const float filtered = filter_.process(sum);
				const auto decimated = decimator_.process(filtered);
				if (decimated.has_value())
					output = *decimated;
			}
			return output * nominal_level_linear_ * output_gain_current_;
		}

		// Returns the whole instance to a cleared state: every
		// voice inactive with a zeroed oscillator and envelope, the filter and
		// decimator history cleared. Used when a reused slot is handed to a
		// new instance, so no oscillator phase, envelope level, filter
		// integrator, decimator history, or stale `active` flag from the
		// previous occupant can survive into the new sound.
		// `Synth::start` then re-establishes the base parameters and gates the
		// instance's own voices on through `applyBaseBlock`.
		void resetState() noexcept
		{
			filter_.reset();
			decimator_.reset();
			for (ChipVoice& voice : voices_)
			{
				voice.oscillator.reset();
				voice.envelope.reset();
				voice.waveform = Waveform::triangle;
				voice.pulse_width = 0.5f;
				voice.gain_target = 1.0f;
				voice.gain_current = 1.0f;
				voice.gain_increment = 0.0f;
				voice.active = false;
				voice.ring_mod_source = ChipVoice::invalid_voice;
				voice.hard_sync_source = ChipVoice::invalid_voice;
			}
			cutoff_target_ = default_filter_cutoff_hz;
			cutoff_current_ = default_filter_cutoff_hz;
			cutoff_increment_ = 0.0f;
			resonance_target_ = default_filter_resonance_q;
			resonance_current_ = default_filter_resonance_q;
			resonance_increment_ = 0.0f;
			output_gain_target_ = 1.0f;
			output_gain_current_ = 1.0f;
			output_gain_increment_ = 0.0f;
		}

	private:
		// Test-only access to the smoothed ramp state: the accessor lives in the
		// test target, this declaration is the only production footprint.
		friend struct ChipInstanceTestAccess;

		// Steps `current` toward `target` by `increment` and clamps it at the
		// target, so a ramp completes rather than overshooting and climbing
		// past the value the block asked for. A direct `BlockRenderer` caller
		// rendering more than one sub-block after a single `applyBlock` is
		// what would otherwise overshoot.
		static void stepRamp(float& current, float target, float increment) noexcept
		{
			current += increment;
			if (increment > 0.0f)
				current = std::min(current, target);
			else if (increment < 0.0f)
				current = std::max(current, target);
		}

		// The voice's oscillator output for its configured waveform, or the
		// noise sample when the waveform is noise.
		static float oscillatorOutput(const ChipVoice& voice) noexcept
		{
			switch (voice.waveform)
			{
			case Waveform::triangle:
				return voice.oscillator.triangle();
			case Waveform::sawtooth:
				return voice.oscillator.sawtooth();
			case Waveform::pulse:
				return voice.oscillator.pulse();
			case Waveform::noise:
				return voice.noise.sample();
			}
			return 0.0f;
		}

		std::uint32_t sample_rate_hz_;
		std::uint32_t voice_count_;
		std::uint32_t oversampling_factor_;
		// Linear factor realising `nominal_source_level_db` at the chip output:
		// a full-amplitude render at unity gains peaks at that level, the
		// reference every authored gain value is relative to. Computed from the
		// constant at construction.
		float nominal_level_linear_;
		StateVariableFilter filter_;
		Decimator decimator_;
		// Cutoff and resonance are ramped over one sub-block to avoid zipper
		// noise: `advance` steps `*_current` toward
		// `*_target` by `*_increment` once per output sample.
		float cutoff_target_ = default_filter_cutoff_hz;
		float cutoff_current_ = default_filter_cutoff_hz;
		float cutoff_increment_ = 0.0f;
		float resonance_target_ = default_filter_resonance_q;
		float resonance_current_ = default_filter_resonance_q;
		float resonance_increment_ = 0.0f;
		// Default: no response selected, so the filter passes the input
		// through unfiltered until a caller configures one.
		FilterModeSet modes_{.lowpass = false, .bandpass = false, .highpass = false, .notch = false};
		float saturation_ = 0.0f;
		float output_gain_target_ = 1.0f;
		float output_gain_current_ = 1.0f;
		float output_gain_increment_ = 0.0f;
		std::array<ChipVoice, max_voices_per_instance> voices_;
	};
}
