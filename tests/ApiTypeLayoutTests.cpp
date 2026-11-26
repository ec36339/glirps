#include "glirps/Environment.hpp"
#include "glirps/glirps.hpp"
#include "glirps/glirps_handles.hpp"

#include <type_traits>

// Layout checks for the types that cross the free-function boundary by value:
// each must stay standard-layout and trivially copyable.
static_assert(std::is_standard_layout_v<glirps::ControlParams>);
static_assert(std::is_trivially_copyable_v<glirps::ControlParams>);
static_assert(std::is_standard_layout_v<glirps::MixerCallbacks>);
static_assert(std::is_trivially_copyable_v<glirps::MixerCallbacks>);
static_assert(std::is_standard_layout_v<glirps::LogSeverity>);
static_assert(std::is_trivially_copyable_v<glirps::LogSeverity>);
static_assert(std::is_standard_layout_v<glirps::AllocatorCallbacks>);
static_assert(std::is_trivially_copyable_v<glirps::AllocatorCallbacks>);
static_assert(std::is_standard_layout_v<glirps::LogCallbacks>);
static_assert(std::is_trivially_copyable_v<glirps::LogCallbacks>);
static_assert(std::is_standard_layout_v<glirps::Environment>);
static_assert(std::is_trivially_copyable_v<glirps::Environment>);
static_assert(std::is_standard_layout_v<glirps::CreateResult>);
static_assert(std::is_trivially_copyable_v<glirps::CreateResult>);
static_assert(std::is_standard_layout_v<glirps::StartResult>);
static_assert(std::is_trivially_copyable_v<glirps::StartResult>);
static_assert(std::is_standard_layout_v<glirps::ParameterBlock>);
static_assert(std::is_trivially_copyable_v<glirps::ParameterBlock>);
static_assert(std::is_standard_layout_v<glirps::ChipInstanceHandle>);
static_assert(std::is_trivially_copyable_v<glirps::ChipInstanceHandle>);
static_assert(std::is_standard_layout_v<glirps::shims::Span<float>>);
static_assert(std::is_trivially_copyable_v<glirps::shims::Span<float>>);
static_assert(std::is_standard_layout_v<glirps::shims::Span<const float>>);
static_assert(std::is_trivially_copyable_v<glirps::shims::Span<const float>>);
static_assert(std::is_standard_layout_v<decltype(glirps::ParameterBlock::voices)>);
static_assert(std::is_trivially_copyable_v<decltype(glirps::ParameterBlock::voices)>);
