#!/usr/bin/env bash
# Fails if a commit message mentions a forbidden word, ignoring case. Reads
# commit messages only, never file contents.
#
# Usage: check-commit-messages.sh <base>..<head> [text...]
#
# Each extra text is checked too. It is meant for text that becomes part of a
# merge commit message, such as a branch name or a pull request title.
set -euo pipefail

forbidden_word='claude'

if (($# < 1)) || [[ $1 != *..?* ]]; then
	echo "Usage: $0 <base>..<head> [text...]" >&2
	exit 2
fi
base=${1%%..*}
head=${1#*..}
shift

# Without a base, the range would be empty and the check would pass.
if [[ -z $base ]]; then
	echo "::error::The base revision is empty."
	exit 1
fi

# A shallow clone would hide commits, and the check would pass without them.
if [[ $(git rev-parse --is-shallow-repository) == true ]]; then
	echo "::error::The clone is shallow. Check out the full history."
	exit 1
fi

if git rev-parse --quiet --verify "$base^{commit}" >/dev/null; then
	range="$base..$head"
else
	# A new branch has an all-zero base, and a rewritten branch a base that
	# is gone. Checking too much is the safe choice.
	echo "Base '$base' is not in this clone: checking the whole history."
	range=$head
fi

status=0
hits=$(git log --regexp-ignore-case --fixed-strings --grep="$forbidden_word" --format='%h %s' "$range" --)
if [[ -n $hits ]]; then
	echo "::error::Commit messages mention '$forbidden_word':"
	echo "$hits"
	status=1
fi

shopt -s nocasematch
for text in "$@"; do
	if [[ $text == *"$forbidden_word"* ]]; then
		echo "::error::This text mentions '$forbidden_word': $text"
		status=1
	fi
done

if ((status == 0)); then
	echo "Commit message check passed."
fi
exit "$status"
