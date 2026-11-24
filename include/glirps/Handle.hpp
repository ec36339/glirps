#pragma once

/// @file
/// Unique-ownership RAII wrapper for opaque non-pointer handle types.

#include <type_traits>
#include <utility>

/// Portable `[[no_unique_address]]`. MSVC and clang-cl ignore the standard
/// attribute and provide `[[msvc::no_unique_address]]` instead, so an empty
/// deleter would otherwise cost bytes in every `Handle`.
#if defined(_MSC_VER)
#define GLIRPS_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define GLIRPS_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

namespace glirps::shims
{
	/// Sentinel type used to construct or compare a null `Handle`.
	/// @ingroup shims
	struct NullHandle
	{
		constexpr NullHandle() noexcept = default;
	};

	/// The `NullHandle` value.
	/// @ingroup shims
	inline constexpr NullHandle null_handle{};

	/// Describes how `Handle<T>` creates, tests, and destroys values of `T`.
	/// The primary template is intentionally not defined: specialize it for
	/// each handle type before instantiating `Handle<T>`.
	///
	/// A specialization provides:
	/// 1. `DeleterType`, a default-constructible callable with
	///    `void operator()(T) noexcept`. It may be stateless or stateful.
	/// 2. `static constexpr T nullValue() noexcept`, the value of a null
	///    (non-owning) handle.
	/// 3. `static bool isNull(T) noexcept`, whether a value is null.
	/// 4. `static void assertValid(T) noexcept`, called by `Handle::require`.
	///    It may assert, abort, or do nothing.
	/// @tparam T The handle type.
	/// @ingroup shims
	template<typename T>
	struct HandleTraits;

	/// Constrains `Handle` to trivially copyable, non-void, non-reference types.
	/// @ingroup shims
	template<typename T>
	concept HandleValue
		= std::is_trivially_copyable_v<T> && !std::is_void_v<T> && !std::is_reference_v<T>;

	/// Unique-ownership RAII wrapper for non-pointer handle types. The wrapped
	/// value is destroyed by the traits' deleter on scope exit or reassignment.
	///
	/// A stateless `DeleterType` adds no bytes to the wrapper. A stateful
	/// deleter adds its size to every instance, so such a `Handle` should not
	/// be stored in collections.
	///
	/// Consumers should name the owning aliases, `SynthOwner` and
	/// `BlockRendererOwner`, rather than this template.
	/// @tparam T The handle type; trivially copyable, not void, not a reference.
	/// @tparam Traits A complete `HandleTraits` specialization for `T`.
	/// @ingroup shims
	template<HandleValue T, typename Traits = HandleTraits<T>>
	class Handle
	{
	public:
		/// The wrapped handle type.
		using HandleType = T;
		/// The traits type.
		using TraitsType = Traits;
		/// The deleter type.
		using DeleterType = typename Traits::DeleterType;

		/// Constructs a null handle with a default-constructed deleter.
		Handle() noexcept
			: handle_(Traits::nullValue())
			, deleter_()
		{
		}

		/// Constructs a null handle from the `null_handle` sentinel.
		Handle(NullHandle) noexcept
			: Handle()
		{
		}

		/// Constructs a handle owning `h` with a default-constructed deleter.
		/// @param h The handle to own.
		explicit Handle(T h) noexcept
			: handle_(h)
			, deleter_()
		{
		}

		/// Constructs a handle owning `h` with the given deleter.
		/// @param h The handle to own.
		/// @param d The deleter to use.
		Handle(T h, DeleterType d) noexcept(std::is_nothrow_move_constructible_v<DeleterType>)
			: handle_(h)
			, deleter_(std::move(d))
		{
		}

		Handle(const Handle&) = delete;
		Handle& operator=(const Handle&) = delete;

		/// Takes ownership from `other`, leaving it null.
		/// @param other The handle to take from.
		Handle(Handle&& other) noexcept(std::is_nothrow_move_constructible_v<DeleterType>)
			: handle_(other.handle_)
			, deleter_(std::move(other.deleter_))
		{
			other.handle_ = Traits::nullValue();
		}

		/// Destroys the current handle, then takes ownership from `other`,
		/// leaving it null.
		/// @param other The handle to take from.
		/// @return This handle.
		Handle& operator=(Handle&& other) noexcept(std::is_nothrow_move_assignable_v<DeleterType>)
		{
			if (this != &other)
			{
				reset();
				handle_ = other.handle_;
				deleter_ = std::move(other.deleter_);
				other.handle_ = Traits::nullValue();
			}
			return *this;
		}

		/// Destroys the owned handle if it is not null.
		~Handle() noexcept
		{
			if (!Traits::isNull(handle_))
				deleter_(handle_);
		}

		/// Returns the raw handle value without checking for null.
		/// @return The raw handle value.
		T get() const noexcept
		{
			return handle_;
		}

		/// Returns the raw handle value after asserting it is not null through
		/// `Traits::assertValid`.
		/// @return The raw handle value.
		T require() const
		{
			Traits::assertValid(handle_);
			return handle_;
		}

		/// Releases ownership and returns the raw handle. The caller becomes
		/// responsible for its lifetime. The deleter is not called.
		/// @return The raw handle value.
		T release() noexcept
		{
			T h = handle_;
			handle_ = Traits::nullValue();
			return h;
		}

		/// Destroys the owned handle if it is not null and resets to null.
		void reset() noexcept
		{
			if (!Traits::isNull(handle_))
			{
				deleter_(handle_);
				handle_ = Traits::nullValue();
			}
		}

		/// Destroys the owned handle if it is not null and takes ownership of
		/// `h`.
		/// @param h The handle to own.
		void reset(T h) noexcept
		{
			if (!Traits::isNull(handle_))
				deleter_(handle_);
			handle_ = h;
		}

		/// Returns whether a non-null handle is owned.
		/// @return `true` if the owned handle is not null.
		explicit operator bool() const noexcept
		{
			return !Traits::isNull(handle_);
		}

		/// Compares with the null sentinel.
		/// @return `true` if the owned handle is null.
		bool operator==(NullHandle) const noexcept
		{
			return Traits::isNull(handle_);
		}

		/// Returns the deleter.
		/// @return The deleter.
		const DeleterType& getDeleter() const noexcept
		{
			return deleter_;
		}

		/// Returns the deleter.
		/// @return The deleter.
		DeleterType& getDeleter() noexcept
		{
			return deleter_;
		}

	private:
		T handle_;
		GLIRPS_NO_UNIQUE_ADDRESS DeleterType deleter_;
	};
}
