#!/usr/bin/env bash
# trigger-jenkins: start the Jenkins builds for a PR's head commit by approving its waiting bld-*
#                  workflow runs.
#
# The bld-* jobs run in the "jenkins" environment, which holds each run at "Waiting for review"
# until a yugabyte-dev member approves it, so the gh login running this must be in that team.
# Same as clicking "Review deployments" -> Approve on the PR's workflow runs.
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

sha=$(gh api "repos/$repo/pulls/$pr" --jq .head.sha)

bld_runs() {
  gh api "repos/$repo/actions/runs?head_sha=$sha&event=pull_request&per_page=100" \
    --jq '.workflow_runs[] | select(.name | startswith("bld-")) | "\(.id) \(.status) \(.name)"'
}

# Right after a push the runs may still be queued; wait for them to reach the approval gate.
for _ in {1..12}; do
  runs=$(bld_runs)
  if ! grep -qE ' (queued|requested|pending) ' <<<"$runs"; then
    break
  fi
  sleep 10
done

waiting=$(awk '$2 == "waiting"' <<<"$runs")
if [[ -z $waiting ]]; then
  echo "No bld-* runs are waiting for approval on $sha." >&2
  if [[ -n $runs ]]; then
    echo "Runs on that commit:" >&2
    sed 's/^/  /' <<<"$runs" >&2
  else
    echo "None ran: the PR may be a draft, or the commit has [skip ci]." >&2
  fi
  exit 1
fi

while read -r run_id _ name; do
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
