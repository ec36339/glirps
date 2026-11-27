# GLIRPS

GLIRPS, Retro sound effect synthesizer for games inspired by the 6581/8580 SID. It is not a SID emulator. Sounds are generated at run time from short definitions, so no audio files are needed. The grit is adjustable, from lo-fi to clean.

GLIRPS only produces samples; devices, files, timing, and spatial effects are up to your program ([details](docs/INDEX.md#what-it-does-not-do)).

## Requirements

Windows:

- Visual Studio 2022 or later with its **C++ Clang Compiler for Windows** component.

Linux:

- Clang 22 with libc++ (`clang-22`, `libc++-22-dev`, `libc++abi-22-dev`).

Windows and Linux:

- CMake 3.25 or later and Ninja.
- Git, to fetch GoogleTest when tests are built and no installed copy is found.
- Optional: Doxygen, for the API reference and the guides.

Building GLIRPS, also as a dependency, needs a C++23 compiler. Your own code needs only C++20.

## Using GLIRPS

GLIRPS is a static library. Link the `glirps::glirps` target, which makes the public headers available as `<glirps/...>`. As a dependency, GLIRPS by default builds only the library. It adds no warning flags to your code.

With `FetchContent` (CMake 3.24 or later):

```cmake
include(FetchContent)
FetchContent_Declare(glirps
	GIT_REPOSITORY https://github.com/ec36339/glirps.git
	GIT_TAG v0.1.0
	FIND_PACKAGE_ARGS 0.1 CONFIG)
FetchContent_MakeAvailable(glirps)

target_link_libraries(my_game PRIVATE glirps::glirps)
```

If CMake finds a compatible installed package, it uses that instead of downloading. To build against a local checkout, set `FETCHCONTENT_SOURCE_DIR_GLIRPS` to its path.

With an [installed package](#installing), whose prefix is on `CMAKE_PREFIX_PATH`:

```cmake
find_package(glirps 0.1 CONFIG REQUIRED)
target_link_libraries(my_game PRIVATE glirps::glirps)
```

A copy of the repository inside your source tree also works, with `add_subdirectory(glirps)`.

[The tutorial](docs/tutorial.md) walks through a first program, from a sound program to samples, step by step.

## Build

Run these from the repository root. On Windows, no developer prompt is needed.

```
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

On Linux, prefix preset names with `linux-`.

Use `release` in place of `debug` for an optimized build. Build trees go to `out/build/<preset>`.

If an installed GoogleTest does not work, configure with `-DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER`.

### Installing

The `package` and `linux-package` presets build and install the library to `out/install/<preset>`:

```
cmake --preset package
cmake --build --preset package
```

To install somewhere else, set `CMAKE_INSTALL_PREFIX` when configuring.

The package presets build the Release configuration. On Windows, a Debug build of your project needs the Debug library too. Install it into the same prefix from a second build folder:

```
cmake --preset package -B out/build/package-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/package-debug --target install
```

### Examples

[`hello-wav`](examples/hello-wav/main.cpp) renders one voice to a WAV file.

The `debug` and `release` presets build the examples. On Windows:

```
out/build/release/examples/hello-wav/hello-wav.exe hello.wav
```

On Linux, the program is `out/build/linux-release/examples/hello-wav/hello-wav`.

### Sound regression tests

Some tests compare rendered output with stored hashes. They run with Clang and libc++ on x86-64 Linux only. A failure prints the new hash. Change a stored hash only when the sound is meant to change, and say so in the pull request.

### Thread sanitizer

Run the ThreadSanitizer test:

```
cmake --preset linux-tsan
cmake --build --preset linux-tsan --target glirps_tests_tsan
ctest --preset linux-tsan -R "^glirps_tests_tsan$"
```

### Options

`top-level` means `ON` if GLIRPS is the top-level project, otherwise `OFF`.

| Option | Default | Effect |
| --- | --- | --- |
| `GLIRPS_BUILD_TESTS` | `top-level` | Builds the tests. |
| `GLIRPS_BUILD_EXAMPLES` | `top-level` | Builds the examples. |
| `GLIRPS_INSTALL` | `top-level` | Adds the install rules and the CMake package. |
| `GLIRPS_WARNINGS_AS_ERRORS` | `top-level` | Treats warnings in GLIRPS's own targets as errors. |
| `GLIRPS_ENABLE_TSAN` | `OFF` | Adds a ThreadSanitizer-instrumented library and, if tests are on, its test. Needs Clang; not available on Windows. |

### API reference

To build the documentation locally (needs Doxygen):

```
cmake --build --preset debug --target glirps_docs
```

Then open `out/build/debug/docs/html/index.html`.

## Documentation

Start with the [overview](docs/INDEX.md), which links all reference documents. The API reference and the guides of the `develop` branch are published at <https://ec36339.github.io/glirps/>.

## Issues

[Report a bug or request an enhancement](../../issues/new?template=issue.yml). To report a security vulnerability, follow the [security policy](SECURITY.md) instead.

## License

MIT. See [LICENSE](LICENSE).
