#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>

namespace hello_wav
{
	// Why a WAV file could not be written.
	enum class WavWriteError
	{
		// The output file could not be opened for writing.
		file_open_failed,
		// A write to the open file failed or was short.
		file_write_failed,
	};

	// Converts one float sample to 16-bit PCM: scales `glirps::full_scale` to
	// 32767, hard-clips to the representable range, rounds to nearest, and
	// applies no dither.
	[[nodiscard]] std::int16_t toPcm16(float sample) noexcept;

	// Writes a mono 16-bit PCM WAV file from `samples` at `sample_rate_hz`.
	// Each sample passes through `toPcm16`.
	[[nodiscard]] std::expected<void, WavWriteError>
	writeWav(const std::filesystem::path& path, std::span<const float> samples, std::uint32_t sample_rate_hz);
}
