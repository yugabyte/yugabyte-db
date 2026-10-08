#!/usr/bin/env bash
# trigger-jenkins: start the Jenkins builds for a PR's head commit by approving its waiting bld-*
#                  workflow runs.
#
# The bld-* jobs run in the "jenkins" environment, which holds each run at "Waiting for review"
# until a yugabyte-dev member approves it, so the gh login running this must be in that team.
# Same as clicking "Review deployments" -> Approve on the PR's workflow runs.
#
# Exits 0 when it approved runs, or when Jenkins already started for the head commit (an earlier
# approval, or a branch whose bld-* workflows are not gated). Exits 1 when there is nothing to
# approve.
#
# usage: trigger-jenkins <pr-number> [-R owner/repo]

set -euo pipefail

usage() {
  echo "usage: $0 <pr-number> [-R owner/repo]" >&2
  exit 2
}

[[ $# -ge 1 ]] || usage
pr=${1#\#}
shift
repo=yugabyte/yugabyte-db
while getopts "R:" opt; do
  case $opt in
    R) repo=$OPTARG ;;
    *) usage ;;
  esac
done
[[ $pr =~ ^[0-9]+$ ]] || usage

read -r sha < <(gh api "repos/$repo/pulls/$pr" --jq '"\(.head.sha)"')

# One line per run: "<id> <status> <conclusion> <name>". conclusion is null until it completes.
bld_runs() {
  gh api "repos/$repo/actions/runs?head_sha=$sha&event=pull_request&per_page=100" \
    --jq '.workflow_runs[] | select(.name | startswith("bld-"))
          | "\(.id) \(.status) \(.conclusion) \(.name)"'
}

# The runs for a push can take a while to be created and to reach the
# approval gate, and the commit may already carry completed runs,
# so wait for a run that is waiting or already started rather than for any run at all.
for _ in {1..18}; do
  runs=$(bld_runs)
  if ! grep -qE '^[0-9]+ (queued|requested|pending) ' <<<"$runs" &&
     grep -qE '^[0-9]+ (waiting|in_progress) |^[0-9]+ completed success ' <<<"$runs"; then
    break
  fi
  sleep 10
done

waiting=$(awk '$2 == "waiting"' <<<"$runs")
if [[ -z $waiting ]]; then
  if grep -qE '^[0-9]+ (in_progress|completed success) ' <<<"$runs"; then
    echo "Jenkins already started for $sha; nothing to approve."
    exit 0
  fi
  echo "No bld-* runs are waiting for approval on $sha." >&2
  if [[ -n $runs ]]; then
    echo "Runs on that commit:" >&2
    sed 's/^/  /' <<<"$runs" >&2
  else
    echo "None were created; the head commit may have [skip ci]." >&2
  fi
  exit 1
fi

while read -r run_id _ _ name; do
  env_ids=$(gh api "repos/$repo/actions/runs/$run_id/pending_deployments" \
    --jq '.[] | select(.environment.name == "jenkins" and .current_user_can_approve)
          | .environment.id')
  if [[ -z $env_ids ]]; then
    echo "$name: you cannot approve this run; are you in the yugabyte-dev team?" >&2
    exit 1
  fi
  args=()
  for id in $env_ids; do
    args+=(-F "environment_ids[]=$id")
  done
  gh api -X POST "repos/$repo/actions/runs/$run_id/pending_deployments" "${args[@]}" \
    -f state=approved -f comment="trigger jenkins" >/dev/null
  echo "$name: approved (https://github.com/$repo/actions/runs/$run_id)"
done <<<"$waiting"
