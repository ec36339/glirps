add_executable(hello-wav
	main.cpp
	WavWriter.cpp)

target_sources(hello-wav
	PRIVATE
		WavWriter.hpp)

target_link_libraries(hello-wav
	PRIVATE
		glirps::glirps)

target_compile_features(hello-wav PRIVATE cxx_std_23)
target_compile_options(hello-wav PRIVATE ${glirps_compile_options})
