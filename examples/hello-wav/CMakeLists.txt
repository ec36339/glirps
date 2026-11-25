add_executable(hello-wav
	main.cpp
	wav_writer.cpp)

target_sources(hello-wav
	PRIVATE
		wav_writer.hpp)

target_link_libraries(hello-wav
	PRIVATE
		glirps::glirps)

target_compile_features(hello-wav PRIVATE cxx_std_23)
target_compile_options(hello-wav PRIVATE ${glirps_compile_options})
