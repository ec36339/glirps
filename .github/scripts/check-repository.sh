#!/usr/bin/env bash
# Keeps the repository small: fails on a binary file, on Git LFS use, and on a
# large file. Checks the files in the Git index of the repository it is run in.
set -euo pipefail

max_file_bytes=262144 # 256 KB
lfs_pointer_prefix='version https://git-lfs.github.com/spec/'
# Larger than any LFS pointer file.
max_lfs_pointer_bytes=1024

cd "$(git rev-parse --show-toplevel)"
status=0

fail() {
	echo "::error::$1"
	status=1
}

# Git reports no line counts ("-") for a file it detects as binary.
empty_tree=$(git hash-object -t tree /dev/null)
while IFS=$'\t' read -r -d '' added deleted path; do
	if [[ $added == - && $deleted == - ]]; then
		fail "Binary file: $path"
	fi
done < <(git diff --cached --numstat --no-renames -z "$empty_tree")

# Each entry is "<mode> <object> <stage>\t<path>".
while IFS=$'\t' read -r -d '' entry path; do
	read -r mode object _ <<<"$entry"
	if [[ $mode == 160000 ]]; then
		continue # A submodule has no file content here.
	fi

	size=$(git cat-file -s "$object")
	if ((size > max_file_bytes)); then
		fail "File over $max_file_bytes bytes: $path ($size bytes)"
	fi

	if ((size <= max_lfs_pointer_bytes)); then
		content=$(git cat-file blob "$object" | tr -d '\0')
		if [[ $content == "$lfs_pointer_prefix"* ]]; then
			fail "Git LFS pointer: $path"
		fi
	fi

	if [[ ${path##*/} == .gitattributes ]] && git cat-file blob "$object" | grep -q 'filter=lfs'; then
		fail "Git LFS attribute: $path"
	fi
done < <(git ls-files -s -z)

if ((status == 0)); then
	echo "Repository check passed."
fi
exit "$status"
