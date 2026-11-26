#pragma once

/// @file
/// The sound description a synth consumes.

#include "FilterMode.hpp"
#include "GritProfile.hpp"
#include "Table.hpp"
#include "Waveform.hpp"

#include <cstdint>

namespace glirps
{
	/// Semitone index of C-3, the default base pitch.
	inline constexpr std::int32_t default_base_semitone = 36;

	/// Base pitch as a note index plus cents. The note index counts semitones
	/// above C-0. Scientific-pitch strings are parsed by the consumer.
	struct Pitch
	{
		/// Semitones above C-0.
		std::int32_t semitone = default_base_semitone;
		/// Offset from `semitone`, in cents.
		std::int32_t cents = 0;
	};

	/// Envelope parameters in seconds.
	struct EnvelopeSpec
	{
		/// Linear rise from gate on to full level. The time to reach 99% of it.
		float attack_seconds = 0.002f;
		/// Exponential fall from full level to `sustain_level`. The time to come
		/// within 1% of it.
		float decay_seconds = 0.006f;
		/// Level held while the gate stays on, as a linear multiplier.
		float sustain_level = 1.0f;
		/// Exponential fall after gate off. The time to fall from `sustain_level`
		/// to 0.001 of full level (-60 dB), where the voice counts as silent.
		float release_seconds = 0.006f;
	};

	/// Per-voice tables. If a `has_` flag is false, its table is ignored and the
	/// parameter keeps its base value.
	struct VoiceTables
	{
		/// Whether `waveform` is active.
		bool has_waveform = false;
		/// Waveform selection.
		Table<Waveform> waveform;
		/// Whether `arpeggio` is active.
		bool has_arpeggio = false;
		/// Semitone offset from the base pitch.
		Table<std::int32_t> arpeggio;
		/// Whether `pitch_slide` is active.
		bool has_pitch_slide = false;
		/// Cents added to a running pitch accumulator once per effective tick.
		Table<float> pitch_slide;
		/// Whether `pitch_offset` is active.
		bool has_pitch_offset = false;
		/// Absolute pitch offset from the base pitch, in fractional semitones.
		Table<float> pitch_offset;
		/// Whether `pulse_width_delta` is active.
		bool has_pulse_width_delta = false;
		/// Duty added to a running pulse width once per effective tick.
		Table<float> pulse_width_delta;
		/// Whether `pulse_width` is active. Exclusive with
		/// `has_pulse_width_delta`.
		bool has_pulse_width = false;
		/// Absolute pulse width, replacing the base pulse width.
		Table<float> pulse_width;
		/// Whether `gain` is active.
		bool has_gain = false;
		/// Voice gain, as a linear multiplier.
		Table<float> gain;
		/// Whether `gate` is active.
		bool has_gate = false;
		/// Per-voice retrigger gate, ANDed with the gate that `synthSetGate` or
		/// `release_subtick` establishes. If `has_gate` is false, the table never
		/// closes the gate.
		Table<bool> gate;
		/// Narrows `pitch_slide`'s runtime clamp.
		AccumulateBounds pitch_slide_bounds;
		/// Narrows `pulse_width_delta`'s runtime clamp.
		AccumulateBounds pulse_width_delta_bounds;
	};

	/// One voice: base parameters, its own envelope, and its tables.
	struct VoiceProgram
	{
		/// Base waveform, used until a `tables.waveform` value replaces it.
		Waveform waveform = Waveform::pulse;
		/// Base duty cycle of a pulse waveform, in
		/// `[pulse_width_floor, pulse_width_ceiling]`.
		float pulse_width = 0.5f;
		/// Base voice gain, as a linear multiplier.
		float gain = 1.0f;
		/// This voice's own amplitude envelope.
		EnvelopeSpec envelope;
		/// Automation of this voice's parameters over time.
		VoiceTables tables;
		/// Instance time, in subticks, this voice becomes active at. Before
		/// it, the voice's envelope and every `VoiceTables` table hold
		/// inactive. `0` is immediate. Also the time origin of this voice's
		/// tables.
		std::uint32_t start_subtick = 0;
		/// Whether `ring_mod_source` is active.
		bool has_ring_mod_source = false;
		/// Index into this program's `voices`, naming the voice this voice
		/// ring-modulates against. Meaningful only when `has_ring_mod_source`
		/// is true.
		std::uint32_t ring_mod_source = 0;
		/// Whether `hard_sync_source` is active.
		bool has_hard_sync_source = false;
		/// Index into this program's `voices`, naming the voice this voice
		/// hard-syncs to. Meaningful only when `has_hard_sync_source` is
		/// true.
		std::uint32_t hard_sync_source = 0;
	};

	/// Per-chip filter tables. `has_` flags work as in `VoiceTables`.
	struct FilterTables
	{
		/// Whether `cutoff` is active.
		bool has_cutoff = false;
		/// Filter cutoff, in hertz.
		Table<float> cutoff;
		/// Whether `resonance` is active.
		bool has_resonance = false;
		/// Filter resonance, as a Q factor.
		Table<float> resonance;
	};

	/// The filter shared by every voice of one chip instance.
	struct FilterProgram
	{
		/// The responses summed into the filter's output.
		FilterModeSet modes;
		/// Base cutoff, used until a `tables.cutoff` value replaces it.
		float cutoff_hz = 20000.0f;
		/// Base resonance as a Q factor, used until a `tables.resonance` value
		/// replaces it.
		float resonance_q = 0.707f;
		/// Drive into the filter's saturating stage, in the range 0 to 1.
		/// Fixed for the instance's life.
		float saturation = 0.0f;
		/// Automation of the filter's parameters over time.
		FilterTables tables;
	};

	/// Gate behavior. A one-shot sound sets `has_release_subtick` and
	/// `release_subtick`; a sustained sound leaves `has_release_subtick` false and is gated by the consumer
	/// through `synthSetGate`.
	struct GateProgram
	{
		/// Whether `release_subtick` is active.
		bool has_release_subtick = false;
		/// Instance time, in subticks, at which a one-shot sound releases its
		/// gate: the gate is on for every block whose time is below it.
		/// Meaningful only when `has_release_subtick` is true.
		std::uint32_t release_subtick = 0;
	};

	/// One sound in the form this library consumes. Built by the consumer from
	/// its authored representation. Every pointer member is non-owning: the
	/// consumer allocates and owns the backing storage, and keeps it alive for
	/// at least as long as this `SoundProgram` is in use.
	struct SoundProgram
	{
		/// Quantization, stepping, and effective tick rate.
		GritProfile grit;
		/// Pitch every voice starts from, before tables and variation.
		Pitch base_pitch;
		/// Semitone degrees seeded pitch variation snaps to. If `scale_count` is
		/// zero, the seed does not vary the pitch. Non-owning; sized
		/// `scale_count`.
		const std::int32_t* scale = nullptr;
		/// Number of entries `scale` points to.
		std::uint32_t scale_count = 0;
		/// When and how the sound's gate releases.
		GateProgram gate;
		/// The filter shared by every voice.
		FilterProgram filter;
		/// Chip-wide output gain, as a linear multiplier.
		float output_gain = 1.0f;
		/// At least one entry and at most `max_voices_per_instance`.
		/// Non-owning; sized `voice_count`.
		const VoiceProgram* voices = nullptr;
		/// Number of entries `voices` points to.
		std::uint32_t voice_count = 0;
	};
}
