cmake_minimum_required(VERSION 3.24)

project(glirps
	VERSION 0.1.0
	LANGUAGES CXX)

# Tests are built by default only when GLIRPS is the top-level project.
set(glirps_build_tests_default OFF)
if(PROJECT_IS_TOP_LEVEL)
	set(glirps_build_tests_default ON)
endif()
option(GLIRPS_BUILD_TESTS "Build the GLIRPS unit tests" ${glirps_build_tests_default})
# Builds `glirps_tsan` and `glirps_tests_tsan`, instrumented with
# ThreadSanitizer. Needs a Clang toolchain with a TSan runtime (Linux).
option(GLIRPS_ENABLE_TSAN "Build ThreadSanitizer-instrumented library and concurrency tests" OFF)

if(GLIRPS_ENABLE_TSAN AND WIN32)
	message(FATAL_ERROR "GLIRPS_ENABLE_TSAN is not supported on Windows: LLVM provides no ThreadSanitizer runtime for it.")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(glirps_sources
	src/BlockRenderer.cpp
	src/Synth.cpp
	src/glirps.cpp
	src/glirps_render.cpp
	src/impl/DenormalGuard.cpp)

add_library(glirps STATIC ${glirps_sources})
add_library(glirps::glirps ALIAS glirps)

target_sources(glirps
	PUBLIC
		FILE_SET HEADERS
		BASE_DIRS include
		FILES
			include/glirps/Array.hpp
			include/glirps/BlockRendererHandle.hpp
			include/glirps/ChipInstanceHandle.hpp
			include/glirps/ControlParams.hpp
			include/glirps/Environment.hpp
			include/glirps/FilterMode.hpp
			include/glirps/frequency.hpp
			include/glirps/glirps.hpp
			include/glirps/glirps_handles.hpp
			include/glirps/glirps_render.hpp
			include/glirps/GritProfile.hpp
			include/glirps/Handle.hpp
			include/glirps/ParameterBlock.hpp
			include/glirps/SoundProgram.hpp
			include/glirps/Span.hpp
			include/glirps/StartError.hpp
			include/glirps/SynthHandle.hpp
			include/glirps/Table.hpp
			include/glirps/Waveform.hpp
			include/glirps/constants.hpp
	PRIVATE
		src/impl/Allocator.hpp
		src/impl/BesselI0.hpp
		src/impl/BlockRenderer.hpp
		src/impl/ChipInstance.hpp
		src/impl/ControlGrid.hpp
		src/impl/Decimator.hpp
		src/impl/DenormalGuard.hpp
		src/impl/Envelope.hpp
		src/impl/GritStage.hpp
		src/impl/LfsrNoise.hpp
		src/impl/Logger.hpp
		src/impl/Mixer.hpp
		src/impl/Oscillator.hpp
		src/impl/StateVariableFilter.hpp
		src/impl/SumToMonoMixer.hpp
		src/impl/Synth.hpp
		src/impl/VariationRng.hpp)

# Consumers include public headers as "glirps/<Header>.hpp" through the public
# include root. The `impl/` headers are reachable only through the private
# include root.
target_include_directories(glirps
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src)

# The implementation needs C++23 (`std::expected`). The public headers are
# meant to compile as C++20, so consumers only need that.
target_compile_features(glirps PUBLIC cxx_std_20)
target_compile_features(glirps PRIVATE cxx_std_23)

# Compiles every public header on its own, using only the library's public
# usage requirements (C++20). Build the `all_verify_interface_header_sets`
# target to run the check.
set_property(TARGET glirps PROPERTY VERIFY_INTERFACE_HEADER_SETS ON)

# Warning and exception flags for GLIRPS's own targets only, so dependencies
# and consumers are not affected.
if(MSVC)
	set(glirps_compile_options /EHsc /WX)
else()
	set(glirps_compile_options -Werror)
endif()
target_compile_options(glirps PRIVATE ${glirps_compile_options})

# An opt-in ThreadSanitizer-instrumented build of the same sources. TSan only
# tracks happens-before for instrumented code, so a concurrency test that must
# observe races inside GLIRPS links this library instead of `glirps`.
if(GLIRPS_ENABLE_TSAN)
	add_library(glirps_tsan STATIC ${glirps_sources})
	add_library(glirps::glirps_tsan ALIAS glirps_tsan)
	target_include_directories(glirps_tsan
		PUBLIC
			$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
		PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}/src)
	target_compile_features(glirps_tsan PUBLIC cxx_std_20)
	target_compile_features(glirps_tsan PRIVATE cxx_std_23)
	target_compile_options(glirps_tsan PRIVATE ${glirps_compile_options} -fsanitize=thread)
	target_link_options(glirps_tsan PUBLIC -fsanitize=thread)
endif()

# Opt-in API documentation for the public headers. The target is omitted when
# Doxygen is not installed, so configuring without it still succeeds. The
# `Doxyfile` reads its output folder from the environment because Doxygen has
# no command-line override.
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
	add_custom_target(glirps_docs
		COMMAND ${CMAKE_COMMAND} -E env
			GLIRPS_DOCS_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/docs
			${DOXYGEN_EXECUTABLE} Doxyfile
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		COMMENT "Generating GLIRPS API documentation"
		VERBATIM)
endif()

if(GLIRPS_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
