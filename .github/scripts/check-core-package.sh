#!/usr/bin/env bash
# Checks the result of the `linux-package` preset: nothing was fetched, and the
# installed files are exactly the public headers, the library, and the CMake
# package files.
#
# Usage: check-core-package.sh <build-dir> <install-dir>
# Run from the repository root.
set -euo pipefail

if (($# != 2)); then
	echo "Usage: $0 <build-dir> <install-dir>" >&2
	exit 2
fi
build_dir=$1
install_dir=$2
status=0

# Catches a dependency that was fetched without `FetchContent_MakeAvailable`.
if [[ -e $build_dir/_deps ]]; then
	echo "::error::The core build has a dependency folder: $build_dir/_deps"
	status=1
fi

# The lists go through files because the shell would not notice a failing
# command inside a process substitution.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
find include -type f >"$work/headers"
{
	cat "$work/headers"
	echo lib/libglirps.a
	for file in Config ConfigVersion Targets Targets-release; do
		echo "lib/cmake/glirps/glirps$file.cmake"
	done
} | LC_ALL=C sort >"$work/expected"
(cd "$install_dir" && find . ! -type d) >"$work/found"
sed 's|^\./||' "$work/found" | LC_ALL=C sort >"$work/installed"

if ! diff -u --label expected --label installed "$work/expected" "$work/installed"; then
	echo "::error::The installed files differ from the expected ones."
	status=1
fi

if ((status == 0)); then
	echo "Core package check passed."
fi
exit "$status"
