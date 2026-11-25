#!/usr/bin/env bash
# Keeps the repository small: fails when a commit adds or modifies a file that
# is binary or large, or that uses Git LFS.
#
# Each commit of the range is checked on its own, so a file that a later commit
# removes is still found: it would stay in the history. Files that the commits
# do not touch are not checked.
#
# Usage: check-repository.sh <base>..<head>
set -euo pipefail

max_file_bytes=262144 # 256 KB
lfs_pointer_prefix='version https://git-lfs.github.com/spec/'

if (($# != 1)) || [[ $1 != ?*..?* ]]; then
	echo "Usage: $0 <base>..<head>" >&2
	exit 2
fi
base=${1%%..*}
head=${1#*..}
for revision in "$base" "$head"; do
	if ! git rev-parse --quiet --verify "$revision^{commit}" >/dev/null; then
		echo "::error::Not a commit in this clone: $revision"
		exit 1
	fi
done

status=0
fail() {
	echo "::error::$1"
	status=1
}

# Checks one added or modified file version.
check_file() { # object path commit
	local object=$1 path=$2 where="$2 (commit ${3:0:12})"
	local size bytes_without_zeros content

	size=$(git cat-file -s "$object")
	if ((size > max_file_bytes)); then
		fail "File over $max_file_bytes bytes: $where is $size bytes"
		return
	fi

	# A zero byte makes a file binary. The content decides, so attributes
	# cannot hide a binary file.
	bytes_without_zeros=$(git cat-file blob "$object" | tr -d '\0' | wc -c)
	if ((bytes_without_zeros != size)); then
		fail "Binary file: $where"
		return
	fi

	content=$(git cat-file blob "$object")
	if [[ $content == "$lfs_pointer_prefix"* ]]; then
		fail "Git LFS pointer: $where"
	fi
	if [[ ${path##*/} == .gitattributes && $content == *filter=lfs* ]]; then
		fail "Git LFS attribute: $where"
	fi
}

# The listings go through files because the shell would not notice a failing
# command inside a process substitution.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
git rev-list "$base..$head" -- >"$work/commits"

checked=0
while read -r commit; do
	# `-c` lists for a merge only the files that differ from every parent,
	# which are the ones the merge itself changed.
	git diff-tree -r -c --root --no-renames --no-abbrev --no-commit-id -z "$commit" >"$work/changes"
	# Each change is a "<colons><modes> <objects> <status>" field, one colon
	# per parent, followed by a path field.
	while IFS= read -r -d '' change && IFS= read -r -d '' path; do
		colons=${change%%[!:]*}
		parents=${#colons}
		read -r -a fields <<<"${change#"$colons"}"
		mode=${fields[parents]}
		object=${fields[2 * parents + 1]}
		# Skip a deletion, and a submodule, which has no file content here.
		if [[ $mode == 000000 || $mode == 160000 ]]; then
			continue
		fi
		check_file "$object" "$path" "$commit"
		checked=$((checked + 1))
	done <"$work/changes"
done <"$work/commits"

if ((status == 0)); then
	echo "Repository check passed ($checked added or modified file versions)."
fi
exit "$status"
