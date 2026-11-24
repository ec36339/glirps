#pragma once

#include "glirps/Environment.hpp"

#include <gmock/gmock.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

// Test doubles for the allocator and log callbacks of `Environment`.
namespace glirps::test_support
{
	constexpr std::size_t max_tracked_blocks = 4096;
	constexpr std::size_t no_failure = std::numeric_limits<std::size_t>::max();

	// An allocator that tracks every block outside the global heap
	// (over-allocated with `std::malloc` and aligned by hand, since
	// `std::aligned_alloc` is not portable), so the global allocation counter
	// stays an independent witness. Fails the allocation numbered `fail_at`, counting
	// from zero.
	struct TrackingAllocator
	{
		struct Block
		{
			void* memory = nullptr;
			void* raw = nullptr;
			std::size_t size = 0;
			std::size_t alignment = 0;
		};

		std::array<Block, max_tracked_blocks> blocks{};
		std::size_t block_count = 0;
		std::size_t attempt_count = 0;
		std::size_t allocate_calls = 0;
		std::size_t deallocate_calls = 0;
		std::size_t fail_at = no_failure;
		bool misaligned = false;
		bool mismatched_free = false;

		TrackingAllocator() = default;
		TrackingAllocator(const TrackingAllocator&) = delete;
		TrackingAllocator& operator=(const TrackingAllocator&) = delete;
		TrackingAllocator(TrackingAllocator&&) = delete;
		TrackingAllocator& operator=(TrackingAllocator&&) = delete;

		// Frees any block a failed test left behind.
		~TrackingAllocator()
		{
			for (const Block& block : std::span(blocks).first(block_count))
				std::free(block.raw);
		}

		[[nodiscard]] std::size_t getLiveCount() const noexcept { return allocate_calls - deallocate_calls; }

		[[nodiscard]] Environment makeEnvironment() noexcept
		{
			return Environment{
				.allocator = AllocatorCallbacks{.context = this,
					.allocate = &TrackingAllocator::allocate,
					.deallocate = &TrackingAllocator::deallocate},
				.logger = LogCallbacks{},
			};
		}

		static void* allocate(void* context, std::size_t size, std::size_t alignment) noexcept
		{
			auto& self = *static_cast<TrackingAllocator*>(context);
			const std::size_t attempt = self.attempt_count++;
			if (attempt == self.fail_at || self.block_count == self.blocks.size())
				return nullptr;
			const std::size_t rounded = (size + alignment - 1) / alignment * alignment;
			std::size_t space = rounded + alignment - 1;
			void* const raw = std::malloc(space);
			if (raw == nullptr)
				return nullptr;
			void* memory = raw;
			if (std::align(alignment, rounded, memory, space) == nullptr)
			{
				std::free(raw);
				return nullptr;
			}
			++self.allocate_calls;
			if (reinterpret_cast<std::uintptr_t>(memory) % alignment != 0)
				self.misaligned = true;
			self.blocks.at(self.block_count++) = Block{.memory = memory, .raw = raw, .size = size, .alignment = alignment};
			return memory;
		}

		static void deallocate(void* context, void* memory, std::size_t size, std::size_t alignment) noexcept
		{
			auto& self = *static_cast<TrackingAllocator*>(context);
			++self.deallocate_calls;
			for (Block& block : std::span(self.blocks).first(self.block_count))
			{
				if (block.memory != memory)
					continue;
				if (block.size != size || block.alignment != alignment)
					self.mismatched_free = true;
				std::free(block.raw);
				block = self.blocks.at(self.block_count - 1);
				--self.block_count;
				return;
			}
			self.mismatched_free = true;
		}
	};

	struct LogRecord
	{
		LogSeverity severity = LogSeverity::debug;
		std::string message;
		bool terminated = false;
	};

	// Matches a `LogRecord` of `severity` whose message contains `text` and is
	// zero-terminated.
	inline auto isRecord(LogSeverity severity, const std::string& text)
	{
		return testing::AllOf(testing::Field(&LogRecord::severity, severity),
			testing::Field(&LogRecord::message, testing::HasSubstr(text)),
			testing::Field(&LogRecord::terminated, true));
	}

	// Records every message it receives.
	struct RecordingLog
	{
		std::vector<LogRecord> records;

		[[nodiscard]] Environment makeEnvironment(LogSeverity min_severity) noexcept
		{
			return Environment{
				.allocator = AllocatorCallbacks{},
				.logger = LogCallbacks{.context = this, .log = &RecordingLog::log, .min_severity = min_severity},
			};
		}

		static void log(void* context, LogSeverity severity, const char* message, std::size_t length) noexcept
		{
			auto& self = *static_cast<RecordingLog*>(context);
			self.records.push_back(LogRecord{.severity = severity,
				.message = std::string(message, length),
				.terminated = message[length] == '\0'});
		}
	};
}
