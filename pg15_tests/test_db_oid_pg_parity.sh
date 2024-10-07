#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

yb_ctl_destroy_create

diff <(build/latest/bin/yb-admin --master_addresses=$PGHOST:7100 list_namespaces \
  | grep -E "template1|template0|postgres" | sort -k2 | awk '{print $1 " " $2 }') - <<EOT
template1 00000001000030008000000000000000
template0 00000004000030008000000000000000
postgres 00000005000030008000000000000000
EOT
