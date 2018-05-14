#!/bin/bash
# Copyright (c) YugaByte, Inc.

# NOTE:
# This script contains the steps needed to run yugaware driven integration test.
# It is present on scheduler machine at /home/centos/scripts/itest_cron.sh and is part of cron task to run daily as shown below
# This is a reference replica.
#
# The way to enable it as a cron job is to add the following three lines via `crontab -e`:
# PATH=/home/centos/code/devtools/bin:/home/centos/code/google-styleguide/cpplint:/home/centos/tools/google-cloud-sdk/bin:/home/centos/.local/bin:/home/centos/.linuxbrew-yb-build/bin:/home/centos/tools/arcanist/bin:/usr/local/bin:/opt/yugabyte/yb-server/bin:/opt/yugabyte/yugaware/bin:/usr/lib64/ccache:/usr/local/bin:/usr/bin:/usr/local/sbin:/usr/sbin:/opt/apache-maven-3.3.9/bin:/home/centos/.local/bin:/home/centos/bin
# DEVOPS_HOME=/home/centos/code/devops
# 22 22 * * * /home/centos/scripts/itest_cron.sh >> /var/log/itest.log 2>&1

set -euo pipefail

code_root=/home/centos/code/
itest_yw_repo="$code_root"/yugaware
itest_devops_repo="$code_root"/devops

function rebase_repos() {
  cd $itest_devops_repo
  git stash
  git checkout master
  git pull --rebase
  cd bin
  ./install_python_requirements.sh

  cd $itest_yw_repo
  git stash
  git checkout master
  git pull --rebase
}

if [ ! -d "$itest_yw_repo" ]; then
  cd $code_root
  git clone git@bitbucket.org:yugabyte/yugaware.git
fi

if [ ! -d "$itest_devops_repo" ]; then
  cd $code_root
  git clone git@bitbucket.org:yugabyte/devops.git
fi

export DEVOPS_HOME=$itest_devops_repo

# Separated to standalone function for single edit commenting out.
rebase_repos

export ITEST_USER=sched

cd $itest_yw_repo

. "$HOME/.yugabyte/ansible.env"

# DEFAULT SETTING
USE_MAVEN_LOCAL="true" "$itest_yw_repo"/run_itest --perform_edits --perform_upgrade --perform_backup --notify

# Setting to use when testing local yw/devops changes
# unset LD_LIBRARY_PATH; "$itest_yw_repo"/run_itest --perform_edits --notify --local_path $code_root

# Setting to use existing latest build but still notify!
# "$itest_yw_repo"/run_itest --perform_edits --notify --use_latest_deploy

# For testing without notify and with existing latest build on gcp only!
# "$itest_yw_repo"/run_itest --perform_edits --use_latest_deploy --run_universe_test gcp
