#pragma once

#include "glirps/Environment.hpp"

#include <array>
#include <cstddef>
#include <format>
#include <utility>

namespace glirps::impl
{
	// Capacity of the stack buffer a message is formatted into, including the
	// zero terminator. A longer message is truncated.
	inline constexpr std::size_t log_buffer_size = 256;

	// A copy of the consumer's log callback. Without one, every message is
	// dropped.
	class Logger
	{
	public:
		Logger() noexcept = default;

		// Reports to `callbacks`; a null `log` drops every message.
		explicit Logger(const LogCallbacks& callbacks) noexcept
			: callbacks_(callbacks)
		{
		}

		// Reports whether a message of `severity` would reach the callback.
		[[nodiscard]] bool isEnabled(LogSeverity severity) const noexcept
		{
			return callbacks_.log != nullptr && severity >= callbacks_.min_severity;
		}

		// Formats and reports one message. Returns before formatting when
		// `severity` is disabled. Formats into a stack buffer and allocates nothing.
		template<typename... Args>
		void log(LogSeverity severity, std::format_string<Args...> format, Args&&... args) const noexcept
		{
			if (!isEnabled(severity))
				return;
			std::array<char, log_buffer_size> buffer;
			constexpr auto capacity = static_cast<std::ptrdiff_t>(log_buffer_size - 1);
			const auto result = std::format_to_n(buffer.begin(), capacity, format, std::forward<Args>(args)...);
			const std::size_t length = static_cast<std::size_t>(result.out - buffer.begin());
			buffer.at(length) = '\0';
			callbacks_.log(callbacks_.context, severity, buffer.data(), length);
		}

	private:
		LogCallbacks callbacks_;
	};
}
