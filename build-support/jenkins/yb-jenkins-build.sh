#!/bin/bash

set -euo pipefail

function print_help() {
  cat <<-EOT
Usage: ${0##*} <options>
Options:
  -h, --help
    Show help
  --delete-arc-patch-branches
    Delete branches starting with "arcpatch-D..." (except the current branch) so that the Jenkins
    Phabricator plugin does not give up after three attempts.

Environment variables:
  JOB_NAME
    Jenkins job name. 
  BUILD_TYPE
    Passed directly to build-and-test.sh. The default value is determined based on the job name
    if this environment variable is not specified or if the value is "auto".
  YB_NUM_TESTS_TO_RUN
    Maximum number of tests ctest should run before exiting. Used for testing Jenkins scripts.
EOT

}

delete_arc_patch_branches=false

while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help)
      print_help
      exit 0
    ;;
    --delete-arc-patch-branches)
      delete_arc_patch_branches=true
    ;;
    *)
      echo "Invalid option: $1" >&2
      exit 1
  esac
  shift
done

# TODO: determine BUILD_TYPE automatically based on JOB_NAME if not specified.

if "$delete_arc_patch_branches"; then
  echo "Deleting branches starting with 'arcpatch-D'"
  current_branch=$( git rev-parse --abbrev-ref HEAD )
  for branch_name in $( git for-each-ref --format="%(refname)" refs/heads/ ); do
    branch_name=${branch_name#refs/heads/}
    if [[ "$branch_name" =~ ^arcpatch-D ]]; then
      if [ "$branch_name" == "$current_branch" ]; then
        echo "'$branch_name' is the current branch, not deleting."
      else
        ( set -x; git branch -D "$branch_name" )
      fi
    fi
  done
fi

if [ -n "${YB_NUM_TESTS_TO_RUN:-}" ]; then

  if [[ ! "$YB_NUM_TESTS_TO_RUN" =~ ^[0-9]+$ ]]; then
    echo "Invalid number of tests to run: $YB_NUM_TESTS_TO_RUN" >&2
    exit 1
  fi
  export EXTRA_TEST_FLAGS="-I1,$YB_NUM_TESTS_TO_RUN"
fi

export YB_MINIMIZE_VERSION_DEFINES_CHANGES=1
export YB_MINIMIZE_RECOMPILATION=1
export BUILD_PYTHON=0
export BUILD_JAVA=0
export YB_KEEP_BUILD_ARTIFACTS=true

set +e
build-support/jenkins/build-and-test.sh
exit_code=$?
set -e

# Un-gzip build log files for easy viewing in the Jenkins UI.
for f in build/debug/test-logs/*.txt.gz; do
  if [ -f "$f" ]; then
    gzip -d "$f"
  fi
done

exit $exit_code
