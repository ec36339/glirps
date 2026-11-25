# A project that adds GLIRPS through FetchContent. It checks that this gives the
# library only. By default it uses the checkout it is part of.
cmake_minimum_required(VERSION 3.24)

project(glirps_fetch_content_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_GLIRPS)
	cmake_path(SET FETCHCONTENT_SOURCE_DIR_GLIRPS NORMALIZE "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
endif()

include(FetchContent)
FetchContent_Declare(glirps
	GIT_REPOSITORY https://github.com/ec36339/glirps.git
	GIT_TAG v0.1.0)
FetchContent_MakeAvailable(glirps)

foreach(unwanted IN ITEMS glirps_tests glirps_tests_tsan hello-wav gtest glirps_docs)
	if(TARGET ${unwanted})
		message(FATAL_ERROR "A consumer must not get the target `${unwanted}`.")
	endif()
endforeach()
get_target_property(interface_options glirps INTERFACE_COMPILE_OPTIONS)
if(interface_options)
	message(FATAL_ERROR "GLIRPS imposes compile options on consumers: ${interface_options}")
endif()

add_executable(consumer ../main.cpp)
target_link_libraries(consumer PRIVATE glirps::glirps)
