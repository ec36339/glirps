# GLIRPS

GLIRPS (*Gritty Little Instrument for Retro Procedural Sounds*) is a C++ synth sound effect engine for games, built on the model of the MOS 6581/8580 sound chip, the SID. Nothing is recorded and no audio files ship. Every sound is synthesised at run time from a short definition: up to four voices with triangle, sawtooth, pulse, and pitched noise, an envelope on each, one shared resonant filter, and tables of timed values stepped fifty times a second by default, and up to sixteen times faster for a sound that asks for it. A separate grit stage gives the period character, from full hardware grit down to clean modern synthesis.

It is not a SID emulator. The same sound definition and seed produce the same result every time in one process.

## What it does not do

GLIRPS only synthesises. The *consumer*, the program that embeds it, supplies everything else:

- **Devices and output.** It knows nothing about a sound card or an audio callback. It hands back samples.
- **Files.** It parses nothing. The consumer builds a sound program and hands it over.
- **Timing and scheduling.** It does not decide when a sound plays, reads no clock, and owns no transport between threads.
- **Space.** Panning, distance attenuation, bus routing, and a master limiter belong to the mixer stage, which the consumer may replace.

## Requirements

Windows:

1. Visual Studio 2022 or later with its **C++ Clang Compiler for Windows** component.
2. CMake 3.25 or later and Ninja.

Linux:

1. Clang 22 with libc++ (`clang-22`, `libc++-22-dev`, `libc++abi-22-dev`).
2. CMake 3.25 or later and Ninja.

Both:

1. Git, to fetch GoogleTest when tests are built. If an installed GoogleTest does not work, configure with `-DFETCHCONTENT_TRY_FIND_PACKAGE_MODE=NEVER`.
2. Optional: Doxygen, for the API reference and the guides.

Building the library needs C++23. The public headers compile as C++20, so a consumer needs only C++20.

## Build

Run these from the repository root. On Windows the presets find the toolchain themselves, so no developer prompt is needed.

```
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Use `release` in place of `debug` for an optimized build. Build trees go to `out/build/<preset>`.

On Linux, use the presets with the `linux-` prefix:

```
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

| Preset | Platform | Build type |
| --- | --- | --- |
| `debug` | Windows | Debug |
| `release` | Windows | Release |
| `linux-debug` | Linux | Debug |
| `linux-release` | Linux | Release |
| `linux-tsan` | Linux | Debug with ThreadSanitizer |
| `package` | Windows | Release, library only, installed |
| `linux-package` | Linux | Release, library only, installed |

### Package

The `package` and `linux-package` presets build and install the library to `out/install/<preset>`:

```
cmake --preset package
cmake --build --preset package
```

To install somewhere else, set `CMAKE_INSTALL_PREFIX` when configuring.

The presets build the Release configuration. On Windows, a Debug build of your project needs the Debug library too. Install it into the same prefix from a second build folder:

```
cmake --preset package -B out/build/package-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/package-debug --target install
```

### Examples

| Example | What it shows |
| --- | --- |
| [`hello-wav`](examples/hello-wav/main.cpp) | The shortest path from a sound program to audio: one voice, rendered to a WAV file. |

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

| Option | Default | Effect |
| --- | --- | --- |
| `GLIRPS_BUILD_TESTS` | `ON` when GLIRPS is the top-level project, otherwise `OFF` | Builds the tests. |
| `GLIRPS_BUILD_EXAMPLES` | `ON` when GLIRPS is the top-level project, otherwise `OFF` | Builds the examples. |
| `GLIRPS_INSTALL` | `ON` when GLIRPS is the top-level project, otherwise `OFF` | Adds the install rules and the CMake package. |
| `GLIRPS_WARNINGS_AS_ERRORS` | `ON` when GLIRPS is the top-level project, otherwise `OFF` | Treats warnings in GLIRPS's own targets as errors. |
| `GLIRPS_ENABLE_TSAN` | `OFF` | Builds the ThreadSanitizer test. Needs Clang; not available on Windows. |

### API reference

The API reference and the guides of the `develop` branch are published at <https://ec36339.github.io/glirps/>.

When Doxygen is found, the `glirps_docs` target generates them from the public headers and `docs`:

```
cmake --build --preset debug --target glirps_docs
```

The HTML output goes to `out/build/debug/docs/html`; open `index.html` there.

## Using GLIRPS

GLIRPS is a static library. Link the `glirps::glirps` target, which makes the public headers available as `<glirps/...>`. As a dependency of your project, GLIRPS builds the library only: no tests, no examples, and no warning flags for your code.

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

An installed package of the same minor version is used instead when CMake finds one. To build against a local checkout, set `FETCHCONTENT_SOURCE_DIR_GLIRPS` to its path.

With an [installed package](#package), whose prefix is on `CMAKE_PREFIX_PATH`:

```cmake
find_package(glirps 0.1 CONFIG REQUIRED)
target_link_libraries(my_game PRIVATE glirps::glirps)
```

A copy of the repository inside your source tree also works, with `add_subdirectory(glirps)`.

The smallest path from a sound program to samples looks like this. The consumer builds the `SoundProgram` itself, and it must stay alive until the instance is released:

```cpp
#include <glirps/glirps.hpp>

const auto created = glirps::synthCreate(8, 32); // instance slots, voice slots
if (!created)
	return;
const glirps::SynthHandle synth = created.synth;
glirps::synthConfigure(synth, 48000, glirps::default_oversampling_factor);

const glirps::SoundProgram program = /* built by the consumer */;
const auto started = glirps::synthStart(synth, program, /*seed*/ 1234);
if (started)
{
	// Control side, once per base tick (20 ms).
	glirps::ParameterBlock blocks[16];
	const std::uint32_t count = glirps::synthEvaluateTick(
		synth, started.instance, glirps::shims::Span<glirps::ParameterBlock>(blocks, 16));

	// Render side. The consumer spaces the blocks across the tick; see the
	// documentation on parameter blocks.
	for (std::uint32_t i = 0; i < count; ++i)
		glirps::synthApplyBlock(synth, blocks[i]);
	float samples[glirps::sub_block_frame_count];
	glirps::synthRender(synth, glirps::shims::Span<float>(samples, glirps::sub_block_frame_count));

	glirps::synthRelease(synth, started.instance);
}
glirps::synthDestroy(synth);
```

`glirps_render.hpp` declares the block renderer, and the optional `glirps_handles.hpp` provides RAII owners for both handles. [The overview](docs/INDEX.md) says which header holds what.

The GLIRPS API uses `glirps::shims::Span` and `glirps::shims::Array` in place of `std::span` and `std::array`. Build a `Span` from a pointer and a count, from a `std::span`, or with `glirps::shims::toSpan(container)`.

## Documentation

Start with [the introduction and overview](docs/INDEX.md). It links the glossary and the reference documents for the sound program, the chip instance, parameter blocks, the block renderer, and the mixer stage.

## License

MIT. See [LICENSE](LICENSE).
