#!/bin/bash

# NOTE:
# This script contains the steps needed to run yugaware driven integration test.
# It is present on scheduler machine and is part of cron task to run daily.
# This is a reference replica.

set -euo pipefail

itest_repo=/home/centos/code/yugaware

if [ ! -d "$itest_repo" ]; then
  cd /home/centos/code/
  git clone git@bitbucket.org:yugabyte/yugaware.git
fi

cd $itest_repo
git checkout master
git pull --rebase

# The `unset` is needed to make yugabyte build correctly (otherwise hit ELF lib check failures).
unset LD_LIBRARY_PATH; ./run_itest --update_packages yugaware yugabyte devops --perform_tests --perform_edits --notify
