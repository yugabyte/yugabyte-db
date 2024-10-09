#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

yb_ctl_destroy_create

# Issue the upgrade initdb RPC and expect it to return an error, because we're not in upgrade mode.
initdb_output=$(build/latest/bin/yb-admin --init_master_addrs=127.0.0.200:7100 \
  ysql_major_version_catalog_upgrade 2>&1) && exit 1
grep -q "Must be in upgrade mode (FLAGS_TEST_online_pg11_to_pg15_upgrade)" <<< "$initdb_output"

# Issue the rollback RPC and expect it to return an error, because we're not in upgrade mode.
rollback_output=$(build/latest/bin/yb-admin --init_master_addrs=127.0.0.200:7100 \
  rollback_ysql_major_version_upgrade 2>&1) && exit 1
grep -q "Must be in upgrade mode (FLAGS_TEST_online_pg11_to_pg15_upgrade)" \
  <<< "$rollback_output"

exit 0
