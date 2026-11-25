# Runs `hello-wav` and compares the SHA-256 of the WAV file it writes with the
# stored value. A mismatch means the sound output changed. Updating the stored
# value is a deliberate change.
#
# Run with -DHELLO_WAV=<executable> -DOUTPUT=<file> -P CheckHash.cmake.

set(expected_sha256 "672c5dfa47dad6312c3fec550ef9726da61290e0a8f70fd554063968cf588bd9")

foreach(variable IN ITEMS HELLO_WAV OUTPUT)
	if(NOT DEFINED ${variable})
		message(FATAL_ERROR "${variable} is not set.")
	endif()
endforeach()

# A file left by an earlier run must not satisfy the check.
file(REMOVE "${OUTPUT}")
execute_process(COMMAND "${HELLO_WAV}" "${OUTPUT}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
	message(FATAL_ERROR "hello-wav failed: ${result}")
endif()

file(SHA256 "${OUTPUT}" actual_sha256)
if(NOT actual_sha256 STREQUAL expected_sha256)
	message(FATAL_ERROR
		"The hash of the hello-wav output changed.\n"
		"  expected: ${expected_sha256}\n"
		"  actual:   ${actual_sha256}")
endif()
