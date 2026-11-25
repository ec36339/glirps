#!/usr/bin/env bash
# Fails if a commit message mentions a forbidden word, ignoring case. Reads
# commit messages only, never file contents.
#
# Usage: check-commit-messages.sh <revision or range>
set -euo pipefail

forbidden_word='claude'

if (($# != 1)); then
	echo "Usage: $0 <revision or range>" >&2
	exit 2
fi

# A shallow clone would hide commits, and the check would pass without them.
if [[ $(git rev-parse --is-shallow-repository) == true ]]; then
	echo "::error::The clone is shallow. Check out the full history."
	exit 1
fi

hits=$(git log --regexp-ignore-case --fixed-strings --grep="$forbidden_word" --format='%h %s' "$1" --)
if [[ -n $hits ]]; then
	echo "::error::Commit messages mention '$forbidden_word':"
	echo "$hits"
	exit 1
fi
echo "Commit message check passed."
