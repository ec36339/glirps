#pragma once

/// @file
/// The consumer-supplied environment of a synth or a block renderer: memory
/// allocation callbacks and a log callback. Both are optional and C-style (a
/// function pointer plus a `void*` context), like `MixerCallbacks`.

#include <cstddef>
#include <cstdint>

namespace glirps
{
	/// How serious a log message is. Ascending: `debug` is the least serious.
	enum class LogSeverity : std::uint8_t
	{
		/// Detail for diagnosing GLIRPS itself, for example object creation.
		debug,
		/// A recoverable problem, for example a rejected sound program.
		warning,
		/// A failure that makes a call unable to do its job, for example invalid
		/// arguments or an allocation failure.
		error,
	};

	/// Replaces the default C++ allocation of every heap allocation GLIRPS makes
	/// for one object. Both function pointers must be non-null to take effect;
	/// if either is null, the default allocation is used (a debug assertion
	/// flags the partial case). Trivially copyable and standard-layout.
	///
	/// Called synchronously on the thread making the GLIRPS call. Calls from
	/// different threads may run concurrently; synchronize as needed.
	///
	/// The callbacks and `context` must stay valid from the create call until the
	/// matching destroy call returns, because the destroy call deallocates through
	/// them.
	struct AllocatorCallbacks final
	{
		/// Opaque state handed back to every callback.
		void* context = nullptr;
		/// Returns `size` bytes of memory aligned to `alignment`, or null if none
		/// is available. `alignment` is a power of two. GLIRPS reports a null
		/// return as an allocation failure of the create call.
		void* (*allocate)(void* context, std::size_t size, std::size_t alignment) noexcept = nullptr;
		/// Releases `memory`, which an earlier `allocate` call returned for exactly
		/// this `size` and `alignment`.
		void (*deallocate)(void* context, void* memory, std::size_t size, std::size_t alignment) noexcept = nullptr;
	};

	/// Receives the diagnostic messages of one object. If `log` is null, GLIRPS
	/// is silent. Trivially copyable and standard-layout.
	///
	/// A message is formatted only if its severity is at least `min_severity`, so
	/// a disabled message costs only a comparison.
	///
	/// Any GLIRPS function except `synthRender` that takes a handle of an object
	/// created with an environment may call `log`, as may the create function
	/// itself.
	///
	/// Called synchronously on the thread making the GLIRPS call. Calls from
	/// different threads may run concurrently; synchronize as needed. A consumer
	/// that calls GLIRPS from a real-time thread supplies a `log` that is
	/// real-time safe at the severities it enables.
	///
	/// `log` and `context` must stay valid from the create call until the
	/// matching destroy call returns.
	struct LogCallbacks final
	{
		/// Opaque state handed back to `log`.
		void* context = nullptr;
		/// Receives one message. `message` is zero-terminated, `length` is the
		/// number of characters before the terminator, and `message` is valid
		/// only for the duration of the call. The text of a message may change
		/// between versions.
		void (*log)(void* context, LogSeverity severity, const char* message, std::size_t length) noexcept = nullptr;
		/// The lowest severity that is reported. Messages below it are not formatted.
		LogSeverity min_severity = LogSeverity::warning;
	};

	/// The allocator and logging configuration of one synth or block renderer.
	/// Both members are optional; a default-constructed value selects the default
	/// C++ allocation and no logging. Copied into the created object; each object
	/// has its own. Trivially copyable and standard-layout.
	struct Environment final
	{
		/// Memory allocation callbacks, or none for the default allocation.
		AllocatorCallbacks allocator;
		/// The log callback, or none for no logging.
		LogCallbacks logger;
	};
}
