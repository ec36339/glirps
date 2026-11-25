#!/usr/bin/env bash
# For a CI run that builds nothing: waits for the latest run of a commit that
# does build, and succeeds only if all of its builds succeeded.
#
# Usage: check-earlier-builds.sh <head-sha>
#
# Reads runs through `gh`, which needs `GH_TOKEN`. `GITHUB_REPOSITORY` names the
# repository. `POLL_SECONDS` and `MAX_POLLS` change how long it waits.
set -euo pipefail

if (($# != 1)) || [[ -z $1 ]]; then
	echo "Usage: $0 <head-sha>" >&2
	exit 2
fi
head_sha=$1
poll_seconds=${POLL_SECONDS:-20}
max_polls=${MAX_POLLS:-150}
# A run that was just triggered may take a moment to be listed.
max_polls_without_run=6

# The runs to skip have a name with this prefix; see `run-name` in `ci.yml`.
# The other jobs are the ones that do not build.
runs_query='[.workflow_runs[] | select(.display_title | startswith("Text-only run: ") | not)]
	| max_by(.run_number) | if . then "\(.id) \(.status)" else "" end'
builds_query='.jobs[]
	| select(.name != "Earlier build results" and .name != "Repository hygiene" and .name != "CI Result")
	| "\(.conclusion) \(.name)"'

for ((poll = 1; poll <= max_polls; poll++)); do
	run=$(gh api "repos/$GITHUB_REPOSITORY/actions/workflows/ci.yml/runs?head_sha=$head_sha&event=pull_request&per_page=100" \
		--jq "$runs_query")
	if [[ -z $run ]]; then
		if ((poll >= max_polls_without_run)); then
			echo "::error::No run has built commit $head_sha. Push a commit, or close and reopen the pull request."
			exit 1
		fi
	else
		read -r run_id run_status <<<"$run"
		# The whole run has to end, its own gate included, so that the result
		# of this run is the later one.
		if [[ $run_status == completed ]]; then
			break
		fi
		echo "Waiting for run $run_id ($run_status)."
	fi
	if ((poll == max_polls)); then
		echo "::error::Gave up waiting for the builds of commit $head_sha."
		exit 1
	fi
	sleep "$poll_seconds"
done

builds=$(gh api "repos/$GITHUB_REPOSITORY/actions/runs/$run_id/jobs?filter=latest&per_page=100" --jq "$builds_query")
if [[ -z $builds ]]; then
	echo "::error::Run $run_id has no build jobs."
	exit 1
fi
echo "Builds of run $run_id:"
echo "$builds"
if grep -q -v '^success ' <<<"$builds"; then
	echo "::error::Not every build of run $run_id succeeded."
	exit 1
fi
echo "Earlier build check passed."
