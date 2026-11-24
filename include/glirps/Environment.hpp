#pragma once

/// @file
/// The consumer-supplied environment of a synth or a block renderer: memory
/// allocation callbacks and a log callback. Both are optional and C-style (a
/// function pointer plus a `void*` context), like `MixerCallbacks`.
///
/// The environment is passed to `synthCreate` or `blockRendererCreate` and
/// copied into the created object. It is per object, never global: two objects
/// MAY use different environments in one process.

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
	/// for one object. Both function pointers SHALL be non-null to take effect;
	/// if either is null, the default allocation is used (a debug assertion
	/// flags the partial case). Trivially copyable and standard-layout.
	///
	/// The callbacks run synchronously, only during a call of a GLIRPS function,
	/// on the thread that made the call. GLIRPS does not synchronize them: two
	/// threads that call GLIRPS functions MAY run them concurrently, and the
	/// consumer provides any synchronization the callbacks need. GLIRPS calls
	/// them from `synthCreate`, `blockRendererCreate`, `synthDestroy`, and
	/// `blockRendererDestroy`, and from `synthConfigure` and
	/// `blockRendererReconfigure` only when the oversampling factor grows beyond
	/// the one the object was created with (for a synth,
	/// `default_oversampling_factor`). A null return then terminates the program,
	/// because those functions cannot report failure.
	///
	/// The callbacks and `context` SHALL stay valid from the create call until the
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
	/// Any GLIRPS function that takes a handle of an object created with an
	/// environment MAY call `log`, as MAY the create function itself. `log` runs
	/// synchronously, only during such a call, on the thread that made the call.
	/// GLIRPS never calls it at any other time, from another thread, or from a
	/// thread of its own. GLIRPS does not synchronize it: two threads that call
	/// GLIRPS functions MAY run it concurrently, and the consumer provides any
	/// synchronization it needs. A consumer that calls GLIRPS from a real-time
	/// thread supplies a `log` that is real-time safe at the severities it
	/// enables.
	///
	/// `log` and `context` SHALL stay valid from the create call until the
	/// matching destroy call returns.
	struct LogCallbacks final
	{
		/// Opaque state handed back to `log`.
		void* context = nullptr;
		/// Receives one message. `message` is zero-terminated, `length` is the
		/// number of characters before the terminator, and `message` is valid
		/// only for the duration of the call. The text of a message MAY change
		/// between versions.
		void (*log)(void* context, LogSeverity severity, const char* message, std::size_t length) noexcept = nullptr;
		/// The lowest severity that is reported. Messages below it are not formatted.
		LogSeverity min_severity = LogSeverity::warning;
	};

	/// The allocator and logging configuration of one synth or block renderer.
	/// Both members are optional; a default-constructed value selects the default
	/// C++ allocation and no logging. Trivially copyable and standard-layout.
	struct Environment final
	{
		/// Memory allocation callbacks, or none for the default allocation.
		AllocatorCallbacks allocator;
		/// The log callback, or none for no logging.
		LogCallbacks logger;
	};
}
