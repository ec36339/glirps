# Selects the LLVM toolchain bundled with Visual Studio (clang-cl, lld-link,
# llvm-mt). vswhere ships with every Visual Studio 2017+ install at a stable
# path, so no PATH setup or developer prompt is needed.

find_program(GLIRPS_VSWHERE vswhere
	PATHS "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer"
	NO_DEFAULT_PATH)
if(NOT GLIRPS_VSWHERE)
	message(FATAL_ERROR "vswhere not found. Install Visual Studio with the 'C++ Clang Compiler for Windows' component.")
endif()

execute_process(
	COMMAND "${GLIRPS_VSWHERE}" -latest -property installationPath
	OUTPUT_VARIABLE glirps_vs_install
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET)
if(NOT glirps_vs_install)
	message(FATAL_ERROR "No Visual Studio installation found by vswhere.")
endif()
cmake_path(SET glirps_llvm_bin NORMALIZE "${glirps_vs_install}/VC/Tools/Llvm/x64/bin")

foreach(tool IN ITEMS clang-cl lld-link llvm-mt)
	if(NOT EXISTS "${glirps_llvm_bin}/${tool}.exe")
		message(FATAL_ERROR
			"${tool} not found in ${glirps_llvm_bin}. "
			"Install the 'C++ Clang Compiler for Windows' component via the Visual Studio Installer.")
	endif()
endforeach()

if(NOT CMAKE_CXX_COMPILER)
	set(CMAKE_CXX_COMPILER "${glirps_llvm_bin}/clang-cl.exe" CACHE FILEPATH "Path to clang-cl")
endif()
# Dependencies such as GoogleTest enable C as well, so it needs clang-cl too.
if(NOT CMAKE_C_COMPILER)
	set(CMAKE_C_COMPILER "${glirps_llvm_bin}/clang-cl.exe" CACHE FILEPATH "Path to clang-cl")
endif()
if(NOT CMAKE_LINKER)
	set(CMAKE_LINKER "${glirps_llvm_bin}/lld-link.exe" CACHE FILEPATH "Path to lld-link")
endif()
if(NOT CMAKE_MT)
	set(CMAKE_MT "${glirps_llvm_bin}/llvm-mt.exe" CACHE FILEPATH "Path to llvm-mt")
endif()

# CMake runs the resource compiler to embed executable manifests; the Windows
# SDK's rc.exe is not on PATH outside a developer prompt, so pick the newest.
if(NOT CMAKE_RC_COMPILER)
	file(GLOB glirps_kit_dirs "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/10.*")
	list(SORT glirps_kit_dirs COMPARE NATURAL ORDER DESCENDING)
	foreach(kit_dir IN LISTS glirps_kit_dirs)
		if(EXISTS "${kit_dir}/x64/rc.exe")
			set(CMAKE_RC_COMPILER "${kit_dir}/x64/rc.exe" CACHE FILEPATH "Path to rc")
			break()
		endif()
	endforeach()
endif()
