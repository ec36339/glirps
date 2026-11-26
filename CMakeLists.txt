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

set(glirps_warnings_as_errors_default OFF)
if(PROJECT_IS_TOP_LEVEL)
	set(glirps_warnings_as_errors_default ON)
endif()
option(GLIRPS_WARNINGS_AS_ERRORS "Treat warnings in GLIRPS's own targets as errors"
	${glirps_warnings_as_errors_default})

set(glirps_install_default OFF)
if(PROJECT_IS_TOP_LEVEL)
	set(glirps_install_default ON)
endif()
option(GLIRPS_INSTALL "Add install rules and the GLIRPS CMake package" ${glirps_install_default})

set(glirps_build_examples_default OFF)
if(PROJECT_IS_TOP_LEVEL)
	set(glirps_build_examples_default ON)
endif()
option(GLIRPS_BUILD_EXAMPLES "Build the GLIRPS examples" ${glirps_build_examples_default})

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
# A Debug library cannot be linked into a Release program with the MSVC ABI, or
# the reverse, so both need their own file to share an install prefix.
if(MSVC)
	set_target_properties(glirps PROPERTIES DEBUG_POSTFIX d)
endif()

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
set(glirps_compile_options "")
if(MSVC)
	list(APPEND glirps_compile_options /EHsc)
endif()
if(GLIRPS_WARNINGS_AS_ERRORS)
	if(MSVC)
		list(APPEND glirps_compile_options /WX)
	else()
		list(APPEND glirps_compile_options -Werror)
	endif()
endif()
# No floating-point contraction, so a compiler cannot fuse operations and
# change rendered output.
if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	# `clang-cl` uses the precise model by default but still contracts under
	# it, and rejects an explicit `/fp:precise` next to this flag.
	list(APPEND glirps_compile_options /clang:-ffp-contract=off)
elseif(MSVC)
	list(APPEND glirps_compile_options /fp:precise)
else()
	list(APPEND glirps_compile_options -ffp-contract=off)
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

# Documentation target, only when GLIRPS is the top-level project and Doxygen is
# installed.
if(PROJECT_IS_TOP_LEVEL)
	find_package(Doxygen QUIET)
endif()
if(PROJECT_IS_TOP_LEVEL AND DOXYGEN_FOUND)
	add_custom_target(glirps_docs
		COMMAND ${CMAKE_COMMAND} -E env
			GLIRPS_DOCS_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/docs
			${DOXYGEN_EXECUTABLE} Doxyfile
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		COMMENT "Generating GLIRPS API documentation"
		VERBATIM)
endif()

# The package holds the library, its public headers, and the files
# `find_package(glirps CONFIG)` needs, and nothing else.
if(GLIRPS_INSTALL)
	include(GNUInstallDirs)
	include(CMakePackageConfigHelpers)
	set(glirps_package_dir ${CMAKE_INSTALL_LIBDIR}/cmake/glirps)

	install(TARGETS glirps
		EXPORT glirpsTargets
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
		FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
		INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
	install(EXPORT glirpsTargets
		NAMESPACE glirps::
		DESTINATION ${glirps_package_dir})

	configure_package_config_file(cmake/glirpsConfig.cmake.in
		${CMAKE_CURRENT_BINARY_DIR}/glirpsConfig.cmake
		INSTALL_DESTINATION ${glirps_package_dir})
	# A 0.x minor version makes no compatibility promise.
	write_basic_package_version_file(
		${CMAKE_CURRENT_BINARY_DIR}/glirpsConfigVersion.cmake
		COMPATIBILITY SameMinorVersion)
	install(FILES
		${CMAKE_CURRENT_BINARY_DIR}/glirpsConfig.cmake
		${CMAKE_CURRENT_BINARY_DIR}/glirpsConfigVersion.cmake
		DESTINATION ${glirps_package_dir})
endif()

# Before the tests, which check an example's output.
if(GLIRPS_BUILD_EXAMPLES)
	add_subdirectory(examples)
endif()

if(GLIRPS_BUILD_TESTS)
	enable_testing()
	add_subdirectory(tests)
endif()
