#include "GoldenProgramTestSupport.hpp"

#include "glirps/glirps_handles.hpp"

#include "SoundProgramTestSupport.hpp"
#include "SynthApiTestSupport.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <optional>
#include <utility>

namespace glirps::test_support
{
	namespace
	{
		constexpr std::uint32_t render_base_ticks = 30;
		constexpr std::uint32_t release_base_tick = 20;
		constexpr std::uint32_t sub_blocks_per_base_tick = frames_per_base_tick / sub_block_frame_count;
		constexpr std::uint32_t frames_per_rendered_tick = sub_blocks_per_base_tick * sub_block_frame_count;

		void addFilter(SoundProgram& program, FilterModeSet modes) noexcept
		{
			program.filter.modes = modes;
			program.filter.cutoff_hz = 1200.0f;
			program.filter.resonance_q = 4.0f;
		}

		// A pitch offset table of one segment, whose values are four base ticks
		// apart.
		Table<float> makePitchOffsetTable(TestProgramStorage& storage, InterpolationMode mode)
		{
			constexpr std::uint16_t interval = 4 * subticks_per_base_tick;
			return makeSegmentTable<float>(storage,
				{Segment{.value_count = 4, .interval_subticks = interval, .interpolation_mode = mode}},
				{{.min = 0.0f, .max = 0.0f},
					{.min = 7.0f, .max = 7.0f},
					{.min = -3.0f, .max = -3.0f},
					{.min = 5.0f, .max = 5.0f}});
		}

		// Builds the program of `golden`. `storage` SHALL outlive every use of the
		// returned program.
		SoundProgram makeProgram(TestProgramStorage& storage, const GoldenCase& golden)
		{
			SoundProgram& program = storage.program;
			program.grit.tick_multiplier = golden.tick_multiplier;
			program.base_pitch = Pitch{.semitone = 45, .cents = 7};
			program.gate.has_release_subtick = true;
			program.gate.release_subtick = release_base_tick * subticks_per_base_tick;
			// Seeded pitch variation, so different seeds sound different.
			setScale(storage, {0, 2, 3, 5, 7, 8, 10});

			VoiceProgram voice;
			voice.waveform = golden.waveform;
			voice.pulse_width = 0.3f;
			voice.envelope = EnvelopeSpec{.attack_seconds = 0.01f,
				.decay_seconds = 0.05f,
				.sustain_level = 0.6f,
				.release_seconds = 0.08f};
			std::vector<VoiceProgram> voices{voice};

			switch (golden.feature)
			{
			case Feature::none:
				break;
			case Feature::ring_modulation:
			case Feature::hard_sync:
			{
				// A second, silent voice as the modulation source.
				VoiceProgram source = voice;
				source.gain = 0.0f;
				source.tables.has_arpeggio = true;
				source.tables.arpeggio = makeIntTable(storage, {19});
				voices.push_back(source);
				if (golden.feature == Feature::ring_modulation)
				{
					voices.front().has_ring_mod_source = true;
					voices.front().ring_mod_source = 1;
				}
				else
				{
					voices.front().has_hard_sync_source = true;
					voices.front().hard_sync_source = 1;
				}
				break;
			}
			case Feature::lowpass:
				addFilter(program, {.lowpass = true});
				break;
			case Feature::bandpass:
				addFilter(program, {.lowpass = false, .bandpass = true});
				break;
			case Feature::highpass:
				addFilter(program, {.lowpass = false, .highpass = true});
				break;
			case Feature::notch:
				addFilter(program, {.lowpass = false, .notch = true});
				break;
			case Feature::tables:
			{
				VoiceTables& tables = voices.front().tables;
				tables.has_waveform = true;
				tables.waveform = makeWaveformTable(storage, {Waveform::pulse, Waveform::sawtooth}, 0, 4);
				tables.has_arpeggio = true;
				tables.arpeggio = makeIntTable(storage,
					{0, 4, 7},
					0,
					2,
					{std::nullopt, ValueRange<std::int32_t>{.min = 2, .max = 5}});
				tables.has_pitch_slide = true;
				tables.pitch_slide = makeFloatTable(storage, {15.0f});
				tables.has_pulse_width = true;
				tables.pulse_width = makeFloatTable(storage, {0.1f, 0.5f, 0.8f}, 0, 3);
				tables.has_gain = true;
				tables.gain = makeFloatTable(storage, {1.0f, 0.4f}, 0, 5);
				tables.has_gate = true;
				tables.gate = makeBoolTable(storage, {true, true, true, false}, 0, 2);
				addFilter(program, {.lowpass = true});
				program.filter.tables.has_cutoff = true;
				program.filter.tables.cutoff = makeFloatTable(storage, {500.0f, 2000.0f, 6000.0f}, 0, 4);
				break;
			}
			case Feature::pitch_offset_step:
			case Feature::pitch_offset_linear:
			case Feature::pitch_offset_catmull_rom:
			{
				const InterpolationMode mode = golden.feature == Feature::pitch_offset_step ? InterpolationMode::step
					: golden.feature == Feature::pitch_offset_linear                        ? InterpolationMode::linear
																	 : InterpolationMode::catmull_rom;
				voices.front().tables.has_pitch_offset = true;
				voices.front().tables.pitch_offset = makePitchOffsetTable(storage, mode);
				break;
			}
			case Feature::pulse_width_delta:
				voices.front().tables.has_pulse_width_delta = true;
				voices.front().tables.pulse_width_delta = makeFloatTable(storage, {0.02f});
				break;
			case Feature::resonance_table:
				addFilter(program, {.lowpass = true});
				program.filter.tables.has_resonance = true;
				program.filter.tables.resonance = makeFloatTable(storage, {0.7f, 12.0f, 3.0f}, 0, 4);
				break;
			}

			setVoices(storage, std::move(voices));
			return program;
		}
	}

	std::string describe(const RenderError& error)
	{
		if (const auto* create_error = std::get_if<CreateError>(&error))
			return std::format("synthCreate failed with CreateError {}", std::to_underlying(*create_error));
		return std::format("synthStart failed with StartError {}", std::to_underlying(std::get<StartError>(error)));
	}

	std::expected<std::vector<float>, RenderError> renderGoldenCase(const GoldenCase& golden)
	{
		TestProgramStorage storage;
		const SoundProgram program = makeProgram(storage, golden);

		const CreateResult created = synthCreate(1, max_voices_per_instance);
		if (!created)
			return std::unexpected(created.error);
		const SynthOwner synth(created.synth);
		synthConfigure(created.synth, test_sample_rate_hz, test_oversampling_factor);
		synthSetGritScale(created.synth, golden.grit_scale);
		const auto started = synthStart(created.synth, program, golden.seed, golden.onset_delay_base_ticks);
		if (!started)
			return std::unexpected(started.error);

		std::vector<float> samples(std::size_t{render_base_ticks} * frames_per_rendered_tick, 0.0f);
		std::span<float> remaining(samples);
		for (std::uint32_t tick = 0; tick < render_base_ticks; ++tick)
		{
			std::vector<ParameterBlock> blocks(synthGetBlocksPerBaseTick(created.synth, started.instance));
			const std::uint32_t written = synthEvaluateTick(created.synth, started.instance, shims::toSpan(blocks));
			std::uint32_t rendered_sub_blocks = 0;
			std::uint32_t block_index = 0;
			for (const ParameterBlock& block : std::span(blocks).first(written))
			{
				synthApplyBlock(created.synth, block);
				++block_index;
				const std::uint32_t end = sub_blocks_per_base_tick * block_index / written;
				const std::size_t frames = std::size_t{end - rendered_sub_blocks} * sub_block_frame_count;
				rendered_sub_blocks = end;
				if (frames == 0)
					continue;
				synthRender(created.synth, shims::toSpan(remaining.first(frames)));
				remaining = remaining.subspan(frames);
			}
		}
		synthRelease(created.synth, started.instance);
		return samples;
	}

	std::uint64_t hashPcm16(std::span<const float> samples) noexcept
	{
		constexpr std::uint64_t offset_basis = 0xcbf29ce484222325ULL;
		constexpr std::uint64_t prime = 0x100000001b3ULL;
		constexpr float pcm_scale = static_cast<float>(std::numeric_limits<std::int16_t>::max());
		constexpr float pcm_min = static_cast<float>(std::numeric_limits<std::int16_t>::min());
		constexpr std::uint32_t bits_per_byte = std::numeric_limits<std::uint8_t>::digits;
		constexpr std::uint32_t byte_mask = std::numeric_limits<std::uint8_t>::max();

		std::uint64_t hash = offset_basis;
		const auto add = [&hash](std::uint32_t byte) { hash = (hash ^ (byte & byte_mask)) * prime; };
		for (const float sample : samples)
		{
			const float scaled = std::clamp(sample * (pcm_scale / full_scale), pcm_min, pcm_scale);
			const auto pcm = static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(scaled)));
			add(pcm);
			add(pcm >> bits_per_byte);
		}
		return hash;
	}
}
