#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace glirps::impl
{
	// The sixteen reference attack durations, exposed as named presets. Decay
	// and release are exactly three times the attack entry of the same index.
	inline constexpr float envelope_rate_multiplier = 3.0f;
	inline constexpr float attack_0 = 0.002f;
	inline constexpr float attack_1 = 0.00348f;
	inline constexpr float attack_2 = 0.00604f;
	inline constexpr float attack_3 = 0.0105f;
	inline constexpr float attack_4 = 0.0183f;
	inline constexpr float attack_5 = 0.0317f;
	inline constexpr float attack_6 = 0.0552f;
	inline constexpr float attack_7 = 0.0959f;
	inline constexpr float attack_8 = 0.167f;
	inline constexpr float attack_9 = 0.290f;
	inline constexpr float attack_10 = 0.504f;
	inline constexpr float attack_11 = 0.876f;
	inline constexpr float attack_12 = 1.52f;
	inline constexpr float attack_13 = 2.65f;
	inline constexpr float attack_14 = 4.60f;
	inline constexpr float attack_15 = 8.00f;

	inline constexpr float decay_0 = envelope_rate_multiplier * attack_0;
	inline constexpr float decay_1 = envelope_rate_multiplier * attack_1;
	inline constexpr float decay_2 = envelope_rate_multiplier * attack_2;
	inline constexpr float decay_3 = envelope_rate_multiplier * attack_3;
	inline constexpr float decay_4 = envelope_rate_multiplier * attack_4;
	inline constexpr float decay_5 = envelope_rate_multiplier * attack_5;
	inline constexpr float decay_6 = envelope_rate_multiplier * attack_6;
	inline constexpr float decay_7 = envelope_rate_multiplier * attack_7;
	inline constexpr float decay_8 = envelope_rate_multiplier * attack_8;
	inline constexpr float decay_9 = envelope_rate_multiplier * attack_9;
	inline constexpr float decay_10 = envelope_rate_multiplier * attack_10;
	inline constexpr float decay_11 = envelope_rate_multiplier * attack_11;
	inline constexpr float decay_12 = envelope_rate_multiplier * attack_12;
	inline constexpr float decay_13 = envelope_rate_multiplier * attack_13;
	inline constexpr float decay_14 = envelope_rate_multiplier * attack_14;
	inline constexpr float decay_15 = envelope_rate_multiplier * attack_15;

	inline constexpr float release_0 = envelope_rate_multiplier * attack_0;
	inline constexpr float release_1 = envelope_rate_multiplier * attack_1;
	inline constexpr float release_2 = envelope_rate_multiplier * attack_2;
	inline constexpr float release_3 = envelope_rate_multiplier * attack_3;
	inline constexpr float release_4 = envelope_rate_multiplier * attack_4;
	inline constexpr float release_5 = envelope_rate_multiplier * attack_5;
	inline constexpr float release_6 = envelope_rate_multiplier * attack_6;
	inline constexpr float release_7 = envelope_rate_multiplier * attack_7;
	inline constexpr float release_8 = envelope_rate_multiplier * attack_8;
	inline constexpr float release_9 = envelope_rate_multiplier * attack_9;
	inline constexpr float release_10 = envelope_rate_multiplier * attack_10;
	inline constexpr float release_11 = envelope_rate_multiplier * attack_11;
	inline constexpr float release_12 = envelope_rate_multiplier * attack_12;
	inline constexpr float release_13 = envelope_rate_multiplier * attack_13;
	inline constexpr float release_14 = envelope_rate_multiplier * attack_14;
	inline constexpr float release_15 = envelope_rate_multiplier * attack_15;

	// One rate preset: attack, decay, and release durations in seconds.
	struct EnvelopeRateEntry
	{
		// Time from gate on to peak level.
		float attack_seconds;
		// Time from peak level to the sustain level.
		float decay_seconds;
		// Time from gate off to silence.
		float release_seconds;
	};

	// Number of rate presets, one per value of the reference hardware's
	// 4-bit rate register.
	inline constexpr std::size_t envelope_rate_preset_count = 16;

	// The rate presets, index-ordered so a rate index selects one.
	inline constexpr std::array<EnvelopeRateEntry, envelope_rate_preset_count> envelope_rate_presets = {{
		{.attack_seconds = attack_0, .decay_seconds = decay_0, .release_seconds = release_0},
		{.attack_seconds = attack_1, .decay_seconds = decay_1, .release_seconds = release_1},
		{.attack_seconds = attack_2, .decay_seconds = decay_2, .release_seconds = release_2},
		{.attack_seconds = attack_3, .decay_seconds = decay_3, .release_seconds = release_3},
		{.attack_seconds = attack_4, .decay_seconds = decay_4, .release_seconds = release_4},
		{.attack_seconds = attack_5, .decay_seconds = decay_5, .release_seconds = release_5},
		{.attack_seconds = attack_6, .decay_seconds = decay_6, .release_seconds = release_6},
		{.attack_seconds = attack_7, .decay_seconds = decay_7, .release_seconds = release_7},
		{.attack_seconds = attack_8, .decay_seconds = decay_8, .release_seconds = release_8},
		{.attack_seconds = attack_9, .decay_seconds = decay_9, .release_seconds = release_9},
		{.attack_seconds = attack_10, .decay_seconds = decay_10, .release_seconds = release_10},
		{.attack_seconds = attack_11, .decay_seconds = decay_11, .release_seconds = release_11},
		{.attack_seconds = attack_12, .decay_seconds = decay_12, .release_seconds = release_12},
		{.attack_seconds = attack_13, .decay_seconds = decay_13, .release_seconds = release_13},
		{.attack_seconds = attack_14, .decay_seconds = decay_14, .release_seconds = release_14},
		{.attack_seconds = attack_15, .decay_seconds = decay_15, .release_seconds = release_15},
	}};

	// The crossing levels the segment durations are defined against: the
	// attack peak, the decay's proximity to the sustain level as a fraction
	// of it, and the release silence threshold.
	inline constexpr float attack_crossing_level = 0.99f;
	inline constexpr float decay_proximity = 0.01f;
	inline constexpr float decay_hold_ratio = 1.0f + decay_proximity;
	inline constexpr float release_silence_level = 0.001f;

	// ADSR envelope generator with linear attack, exponential decay and
	// release, and sustain as a level rather than a rate. Gate on starts
	// attack; gate off starts release from the current level.
	class Envelope
	{
	public:
		Envelope() noexcept = default;

		// Constructs an envelope advancing at `sample_rate_hz`.
		explicit Envelope(std::uint32_t sample_rate_hz) noexcept
			: sample_rate_hz_(sample_rate_hz)
		{
		}

		// Selects the attack, decay, and release rates by preset index and the
		// sustain level. Returns whether every index is in range.
		bool setRates(std::uint32_t attack_index,
			std::uint32_t decay_index,
			std::uint32_t release_index,
			float sustain_level) noexcept
		{
			if (attack_index >= envelope_rate_presets.size() || decay_index >= envelope_rate_presets.size()
				|| release_index >= envelope_rate_presets.size())
				return false;
			const EnvelopeRateEntry& attack = envelope_rate_presets.at(attack_index);
			const EnvelopeRateEntry& decay = envelope_rate_presets.at(decay_index);
			const EnvelopeRateEntry& release = envelope_rate_presets.at(release_index);
			// Linear attack reaching `attack_crossing_level` at
			// `attack_seconds`, so the attack duration matches the table
			// directly.
			attack_increment_ = attack_crossing_level / (attack.attack_seconds * static_cast<float>(sample_rate_hz_));
			decay_coefficient_ = decayCoefficient(decay.decay_seconds, sustain_level, sample_rate_hz_);
			release_coefficient_ = sustain_level <= 0.0f
				? 0.0f
				: releaseCoefficient(release.release_seconds, sustain_level, sample_rate_hz_);
			sustain_level_ = sustain_level;
			return true;
		}

		// Selects the attack, decay, and release rates from durations in
		// seconds and the sustain level, converted once rather than per
		// sample. The rate computation is the same as the preset path; only
		// the durations' source differs.
		void setRatesInSeconds(float attack_seconds,
			float decay_seconds,
			float release_seconds,
			float sustain_level) noexcept
		{
			attack_increment_ = attack_crossing_level / (attack_seconds * sample_rate_hz_);
			decay_coefficient_ = decayCoefficient(decay_seconds, sustain_level, sample_rate_hz_);
			release_coefficient_
				= sustain_level <= 0.0f ? 0.0f : releaseCoefficient(release_seconds, sustain_level, sample_rate_hz_);
			sustain_level_ = sustain_level;
		}

		// Gate on starts attack; gate off starts release from the current
		// level. Edge-triggered: a repeated gate value has no effect, so a
		// voice that never gated on stays idle rather than releasing, per
		// `isComplete`'s own note.
		void setGate(bool gate_on) noexcept
		{
			if (gate_on_ == gate_on)
				return;
			gate_on_ = gate_on;
			if (gate_on_)
				phase_ = Phase::attack;
			else
				phase_ = Phase::release;
		}

		// Returns the envelope to its construction state: gated off, level
		// zero, idle. Used when a chip slot is handed to a new instance,
		// so a re-triggered slot starts from silence rather than from whatever
		// the previous occupant's last phase and level were.
		void reset() noexcept
		{
			gate_on_ = false;
			phase_ = Phase::idle;
			level_ = 0.0f;
			sustain_level_ = 1.0f;
		}

		// Advances one sample and returns the current envelope level.
		float advance() noexcept
		{
			switch (phase_)
			{
			case Phase::idle:
				// Silent and stationary until the first gate-on. Without
				// this the attack segment would run on an ungated voice,
				// sounding before a voice deferred by `start_subtick` is due,
				// or before an instance onset deferred to the control grid is
				// due.
				break;
			case Phase::attack:
				level_ += attack_increment_;
				if (level_ >= 1.0f)
				{
					level_ = 1.0f;
					phase_ = Phase::decay;
				}
				break;
			case Phase::decay:
				level_ = sustain_level_ + (level_ - sustain_level_) * decay_coefficient_;
				if (sustain_level_ <= 0.0f && level_ < release_silence_level)
				{
					// A zero sustain decays toward silence; snap once the
					// tail is inaudible so the envelope terminates instead of
					// approaching zero asymptotically.
					level_ = 0.0f;
					phase_ = Phase::sustain;
				}
				else if (level_ <= sustain_level_ * decay_hold_ratio)
				{
					level_ = sustain_level_;
					phase_ = Phase::sustain;
				}
				break;
			case Phase::sustain:
				level_ = sustain_level_;
				break;
			case Phase::release:
				level_ *= release_coefficient_;
				if (level_ < release_silence_level)
					level_ = 0.0f;
				break;
			}
			return level_;
		}

		// Returns true once release has decayed to silence, and for an
		// envelope still idle: it is silent, and it can no longer reach
		// release on its own, because `setGate` is edge-triggered and a gate
		// that never opened has no falling edge to take. A voice held
		// inactive past its instance's release time ends in exactly that
		// state -- by `start_subtick` or by an instance onset delay that
		// quantizes onset to the control grid -- and without this its chip
		// instance would never report complete and its pool slot would never
		// be reclaimed.
		// Reporting complete before the voice has sounded is prevented one
		// level up, by `Synth::render` requiring the chip-wide gate to have
		// closed before it latches `completed_generation`.
		[[nodiscard]] bool isComplete() const noexcept
		{
			if (phase_ == Phase::idle)
			{
				return true;
			}
			return phase_ == Phase::release && level_ == 0.0f;
		}

	private:
		enum class Phase
		{
			// Before the first gate-on: silent, stationary, and producing no
			// segment. Distinct from `attack` at level zero, which is a
			// running attack that has only just started.
			idle,
			attack,
			decay,
			sustain,
			release,
		};

		// Exponential per-sample coefficient for the decay segment. Reaches
		// within `decay_proximity` of a positive sustain level in
		// `duration_seconds`. At or below a
		// sustain of zero the coefficient instead falls to the release silence
		// level, because `segmentCoefficient`'s ratio is `proximity * sustain /
		// (1 - sustain)` and that is identically zero at a sustain of zero,
		// which would collapse the whole decay to one sample.
		static float
		decayCoefficient(float duration_seconds, float sustain_level, std::uint32_t sample_rate_hz) noexcept
		{
			if (sustain_level >= 1.0f)
				return 0.0f;
			if (sustain_level <= 0.0f)
			{
				// Fall from full level to the release silence level over the
				// decay duration, which is the same shape as a release from a
				// full sustain.
				return releaseCoefficient(duration_seconds, 1.0f, sample_rate_hz);
			}
			return segmentCoefficient(duration_seconds, sustain_level, decay_proximity, sample_rate_hz);
		}

		// Exponential per-sample coefficient for a segment that reaches within
		// `proximity` of its target level (as a fraction of the target) in
		// `duration_seconds`. Decay approaches the sustain level, so the
		// crossing is measured at `(1 + proximity) * sustain`.
		static float segmentCoefficient(float duration_seconds,
			float sustain_level,
			float proximity,
			std::uint32_t sample_rate_hz) noexcept
		{
			const float rate = static_cast<float>(sample_rate_hz);
			const float span_samples = duration_seconds * rate;
			// (level - sustain) after the segment is `proximity * sustain`, so
			// the per-sample factor is the span root of that ratio, with the
			// starting level taken as full scale (1.0).
			const float ratio = proximity * sustain_level / (1.0f - sustain_level);
			return std::pow(ratio, 1.0f / span_samples);
		}

		// Exponential per-sample coefficient that reaches
		// `release_silence_level` (absolute) in `duration_seconds` from the
		// sustain level.
		static float
		releaseCoefficient(float duration_seconds, float sustain_level, std::uint32_t sample_rate_hz) noexcept
		{
			const float rate = static_cast<float>(sample_rate_hz);
			const float span_samples = duration_seconds * rate;
			return std::pow(release_silence_level / sustain_level, 1.0f / span_samples);
		}

		std::uint32_t sample_rate_hz_ = 0;
		bool gate_on_ = false;
		Phase phase_ = Phase::idle;
		float level_ = 0.0f;
		float attack_increment_ = 0.0f;
		float decay_coefficient_ = 0.0f;
		float release_coefficient_ = 0.0f;
		float sustain_level_ = 1.0f;
	};
}
