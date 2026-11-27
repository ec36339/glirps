include(FetchContent)

FetchContent_Declare(
	googletest
	GIT_REPOSITORY https://github.com/google/googletest.git
	# The commit of v1.15.2, as a tag can be moved. A shallow clone cannot
	# fetch a commit by its hash.
	GIT_TAG b514bdc898e2951020cbdca1304b75f5950d1f59
	FIND_PACKAGE_ARGS NAMES GTest)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
# Match the dynamic CRT that the glirps targets use.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

if(NOT TARGET GTest::gmock)
	message(FATAL_ERROR
		"The installed GoogleTest has no GoogleMock. To build the pinned version instead, "
		"configure with -DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER.")
endif()

set(glirps_test_sources
	ApiTypeLayoutTests.cpp
	ArrayTests.cpp
	BlockRendererEnvelopeTests.cpp
	BlockRendererFilterTests.cpp
	BlockRendererModulationTests.cpp
	BlockRendererOutputTests.cpp
	BlockRendererParameterBlockTests.cpp
	BlockRendererTests.cpp
	BlockRendererVoiceTests.cpp
	ChipInstanceTests.cpp
	ControlGridTests.cpp
	DecimatorTests.cpp
	EnvelopeCrossRateTests.cpp
	EnvelopeTests.cpp
	EnvironmentTests.cpp
	GoldenHashTests.cpp
	GoldenProgramTests.cpp
	GritStageTests.cpp
	LfsrNoiseTests.cpp
	LoggerTests.cpp
	OscillatorTests.cpp
	PitchTests.cpp
	SpanTests.cpp
	StateVariableFilterTests.cpp
	SynthAllocationTests.cpp
	SynthConcurrencyTests.cpp
	SynthControlParamsTests.cpp
	SynthExternalControlOneShotTests.cpp
	SynthLifecycleTests.cpp
	SynthMixerTests.cpp
	SynthTests.cpp
	SynthSegmentTableTests.cpp)

set(glirps_test_support_sources
	support/golden_programs.cpp
	support/ScopedAllocationCounter.cpp)

set(glirps_test_support_headers
	support/BlockRendererApiTestSupport.hpp
	support/ChipInstanceTestAccess.hpp
	support/EnvironmentTestSupport.hpp
	support/golden_programs.hpp
	support/ScopedAllocationCounter.hpp
	support/SoundProgramTestSupport.hpp
	support/SynthApiTestSupport.hpp)

add_executable(glirps_tests
	${glirps_test_sources}
	${glirps_test_support_sources})

target_sources(glirps_tests
	PRIVATE
		${glirps_test_support_headers})

# `src/` gives the tests access to the private `impl/` headers.
target_include_directories(glirps_tests
	PRIVATE
		${PROJECT_SOURCE_DIR}/src
		${CMAKE_CURRENT_SOURCE_DIR}/support)

target_link_libraries(glirps_tests
	PRIVATE
		glirps::glirps
		GTest::gmock
		GTest::gtest_main)

target_compile_features(glirps_tests PRIVATE cxx_std_23)
target_compile_options(glirps_tests PRIVATE ${glirps_compile_options})

# One ctest entry per executable, not per GoogleTest case.
add_test(NAME glirps_tests COMMAND glirps_tests)

# The same concurrency test, linked against the instrumented `glirps_tsan` so
# ThreadSanitizer can observe races inside the library. Also built into
# `glirps_tests` against `glirps`.
if(GLIRPS_ENABLE_TSAN)
	# Does not compile `ScopedAllocationCounter.cpp`: its global `operator new`
	# replacement would collide with the one ThreadSanitizer installs.
	add_executable(glirps_tests_tsan
		SynthConcurrencyTests.cpp)
	target_sources(glirps_tests_tsan
		PRIVATE
			${glirps_test_support_headers})
	target_compile_options(glirps_tests_tsan PRIVATE ${glirps_compile_options} -fsanitize=thread)
	target_link_options(glirps_tests_tsan PRIVATE -fsanitize=thread)
	target_include_directories(glirps_tests_tsan
		PRIVATE
			${PROJECT_SOURCE_DIR}/src
			${CMAKE_CURRENT_SOURCE_DIR}/support)
	target_link_libraries(glirps_tests_tsan
		PRIVATE
			glirps::glirps_tsan
			GTest::gmock
			GTest::gtest_main)
	add_test(NAME glirps_tests_tsan COMMAND glirps_tests_tsan)
endif()

# The hash of the example's output holds for Clang with libc++ on x86-64 Linux
# only, like the golden hashes.
set(glirps_stored_hashes_apply OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
		AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
	include(CheckCXXSourceCompiles)
	# Compiling is enough to see which standard library is in use.
	set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
	check_cxx_source_compiles("
		#include <cstddef>
		#ifndef _LIBCPP_VERSION
		#error not libc++
		#endif
		int main() { return 0; }" GLIRPS_USES_LIBCXX)
	set(glirps_stored_hashes_apply ${GLIRPS_USES_LIBCXX})
endif()
if(glirps_stored_hashes_apply AND TARGET hello-wav)
	add_test(NAME hello_wav_hash
		COMMAND ${CMAKE_COMMAND}
			-DHELLO_WAV=$<TARGET_FILE:hello-wav>
			-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/hello_wav.wav
			-P ${CMAKE_CURRENT_SOURCE_DIR}/hello_wav/CheckHash.cmake)
endif()
