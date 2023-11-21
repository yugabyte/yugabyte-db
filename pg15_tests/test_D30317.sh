#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

yb_ctl_wipe_restart
bin/ysqlsh -X -v "ON_ERROR_STOP=1" <<EOT
CREATE TABLE t (a int);
EOT
diff <(build/latest/bin/yb-admin --master_addresses=$PGHOST:7100 list_tables include_table_id \
  | grep -E "^(yugabyte.t|template1.pg_class) " | sort) - <<EOT
template1.pg_class 000000010000300080010000000004eb
yugabyte.t 00003491000030008000000000004000
EOT
