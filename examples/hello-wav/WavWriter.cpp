#include "WavWriter.hpp"

#include <glirps/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>

namespace hello_wav
{
	namespace
	{
		// `glirps::full_scale` maps to this 16-bit PCM value. 32767 rather than
		// 32768 keeps the mapping symmetric, so positive full scale does not
		// clip.
		constexpr std::int16_t full_scale_pcm = 32767;
		constexpr float pcm_clip_negative = -32768.0f;
		constexpr float pcm_clip_positive = static_cast<float>(full_scale_pcm);

		constexpr std::array<char, 4> riff_tag = {'R', 'I', 'F', 'F'};
		constexpr std::array<char, 4> wave_tag = {'W', 'A', 'V', 'E'};
		constexpr std::array<char, 4> fmt_tag = {'f', 'm', 't', ' '};
		constexpr std::array<char, 4> data_tag = {'d', 'a', 't', 'a'};

		constexpr std::uint32_t fmt_chunk_size = 16;
		// Bytes the RIFF chunk holds besides the sample data.
		constexpr std::uint32_t riff_header_size = 36;
		constexpr std::uint16_t audio_format_pcm = 1;
		constexpr std::uint16_t channel_count_mono = 1;
		constexpr std::uint16_t bytes_per_sample = 2;
		constexpr std::uint16_t bits_per_sample_pcm = 16;
		// The RIFF chunk size, header included, is a 32-bit field.
		constexpr std::size_t max_sample_count
			= (std::numeric_limits<std::uint32_t>::max() - riff_header_size) / bytes_per_sample;

		constexpr std::uint32_t byte_mask = 0xff;
		constexpr std::uint32_t bits_per_byte = 8;

		void writeTag(std::ofstream& file, const std::array<char, 4>& tag)
		{
			file.write(tag.data(), static_cast<std::streamsize>(tag.size()));
		}

		// WAV integers are little-endian whatever the host order.
		void writeU16(std::ofstream& file, std::uint16_t value)
		{
			file.put(static_cast<char>(value & byte_mask));
			file.put(static_cast<char>((value >> bits_per_byte) & byte_mask));
		}

		void writeU32(std::ofstream& file, std::uint32_t value)
		{
			writeU16(file, static_cast<std::uint16_t>(value));
			writeU16(file, static_cast<std::uint16_t>(value >> (2 * bits_per_byte)));
		}
	}

	std::int16_t toPcm16(float sample) noexcept
	{
		// Clamping and rounding give no defined result for a NaN.
		if (std::isnan(sample))
			return 0;
		const float scaled = sample * (static_cast<float>(full_scale_pcm) / glirps::full_scale);
		const float clipped = std::clamp(scaled, pcm_clip_negative, pcm_clip_positive);
		return static_cast<std::int16_t>(std::lround(clipped));
	}

	std::expected<void, WavWriteError>
	writeWav(const std::filesystem::path& path, std::span<const float> samples, std::uint32_t sample_rate_hz)
	{
		if (samples.size() > max_sample_count)
			return std::unexpected(WavWriteError::too_many_samples);

		std::ofstream file(path, std::ios::binary);
		if (!file)
			return std::unexpected(WavWriteError::file_open_failed);

		const auto data_size = static_cast<std::uint32_t>(samples.size()) * bytes_per_sample;

		writeTag(file, riff_tag);
		writeU32(file, riff_header_size + data_size);
		writeTag(file, wave_tag);
		writeTag(file, fmt_tag);
		writeU32(file, fmt_chunk_size);
		writeU16(file, audio_format_pcm);
		writeU16(file, channel_count_mono);
		writeU32(file, sample_rate_hz);
		writeU32(file, sample_rate_hz * bytes_per_sample);
		writeU16(file, bytes_per_sample);
		writeU16(file, bits_per_sample_pcm);
		writeTag(file, data_tag);
		writeU32(file, data_size);

		for (const float sample : samples)
			writeU16(file, static_cast<std::uint16_t>(toPcm16(sample)));

		// Closing flushes, so a failed final write is reported too.
		file.close();
		if (!file)
			return std::unexpected(WavWriteError::file_write_failed);
		return {};
	}
}
