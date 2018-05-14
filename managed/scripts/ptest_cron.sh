 #!/bin/bash
# Copyright (c) YugaByte, Inc.

# NOTE:
# This script contains the steps needed to run yugaware driven integration test.
# It is present on scheduler machine at /home/centos/scripts/ptest_cron.sh and is part of cron task to run daily as shown below
# This is a reference replica.
#
# The way to enable it as a cron job is to add the following three lines via `crontab -e`:
# PATH=/home/centos/code/devtools/bin:/home/centos/code/google-styleguide/cpplint:/home/centos/tools/google-cloud-sdk/bin:/home/centos/.local/bin:/home/centos/.linuxbrew-yb-build/bin:/home/centos/tools/arcanist/bin:/usr/local/bin:/opt/yugabyte/yb-server/bin:/opt/yugabyte/yugaware/bin:/usr/lib64/ccache:/usr/local/bin:/usr/bin:/usr/local/sbin:/usr/sbin:/opt/apache-maven-3.3.9/bin:/home/centos/.local/bin:/home/centos/bin
# DEVOPS_HOME=/home/centos/code/devops
# 11 11 * * * /home/centos/scripts/ptest_cron.sh >> /var/log/ptest.log 2>&1

set -euo pipefail

code_root=/home/centos/code/
ptest_yw_repo="$code_root"/yugaware
ptest_devops_repo="$code_root"/devops

function rebase_repos() {
  cd $ptest_devops_repo
  git stash
  git checkout master
  git pull --rebase
  cd bin
  ./install_python_requirements.sh

  cd $ptest_yw_repo
  git stash
  git checkout master
  git pull --rebase
}

if [ ! -d "$ptest_yw_repo" ]; then
  cd $code_root
  git clone git@bitbucket.org:yugabyte/yugaware.git
fi

if [ ! -d "$ptest_devops_repo" ]; then
  cd $code_root
  git clone git@bitbucket.org:yugabyte/devops.git
fi

export DEVOPS_HOME=$ptest_devops_repo

# Separated to standalone function for single edit commenting out.
rebase_repos

export ITEST_USER=sched

cd $ptest_yw_repo

. "$HOME/.yugabyte/ansible.env"

# DEFAULT
# Run all options against GCP only for now.
"$ptest_yw_repo"/perf_itest --run_all_workload_combos --notify --perf_test_provider gcp

# Need to create AWS loadtester or dyn create/destroy.
# "$ptest_yw_repo"/perf_itest --run_all_workload_combos --notify
