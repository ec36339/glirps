include(FetchContent)

FetchContent_Declare(
	googletest
	GIT_REPOSITORY https://github.com/google/googletest.git
	GIT_TAG v1.15.2
	GIT_SHALLOW TRUE
	FIND_PACKAGE_ARGS NAMES GTest)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
# Match the dynamic CRT that the glirps targets use.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

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
	support/ScopedAllocationCounter.cpp)

set(glirps_test_support_headers
	support/BlockRendererApiTestSupport.hpp
	support/ChipInstanceTestAccess.hpp
	support/EnvironmentTestSupport.hpp
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
# ThreadSanitizer can observe races inside GLIRPS. Also built into
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
