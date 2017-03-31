#!/bin/bash

# NOTE:
# This script contains the steps needed to run yugaware driven integration test.
# It is present on scheduler machine and is part of cron task to run daily.
# This is a reference replica.

set -euo pipefail

code_root=/home/centos/code/
itest_yw_repo="$code_root"/yugaware
itest_devops_repo="$code_root"/devops

if [ ! -d "$itest_yw_repo" ]; then
  cd $code_root
  git clone git@bitbucket.org:yugabyte/yugaware.git
fi

if [ ! -d "$itest_devops_repo" ]; then
  cd $code_root
  git clone git@bitbucket.org:yugabyte/devops.git
fi

export DEVOPS_HOME=$itest_devops_repo

cd $itest_devops_repo
git checkout master
git pull --rebase
cd bin
./install_python_requirements.sh

cd $itest_yw_repo
git checkout master
git pull --rebase

# The `unset` is needed to make yugabyte build correctly (otherwise hit ELF lib check failures).
unset LD_LIBRARY_PATH; "$itest_yw_repo"/run_itest --update_packages yugaware yugabyte devops --perform_tests --perform_edits --notify
