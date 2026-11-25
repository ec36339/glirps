# Fails the configure when the project asks for a dependency, to show that the
# core needs none. Use it with
# -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=<path to this file>.

function(forbid_dependency method)
	set(name "${ARGV1}")
	# Optional: it only adds the API reference target.
	if(method STREQUAL "FIND_PACKAGE" AND name STREQUAL "Doxygen")
		return()
	endif()
	message(FATAL_ERROR "The core must need no dependency, but asks for `${name}` (${method}).")
endfunction()

cmake_language(SET_DEPENDENCY_PROVIDER forbid_dependency
	SUPPORTED_METHODS FIND_PACKAGE FETCHCONTENT_MAKEAVAILABLE_SERIAL)
