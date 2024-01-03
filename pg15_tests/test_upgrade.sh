#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

# Download pg11.
prefix="/tmp"
ybversion_pg11="2.18.4.2"
ybbuild="b2"
if [[ $OSTYPE = linux* ]]; then
  arch="linux-x86_64"
fi
if [[ $OSTYPE = darwin* ]]; then
  arch="darwin-x86_64"
fi
ybfilename_pg11="yugabyte-$ybversion_pg11-$ybbuild-$arch.tar.gz"

if [ ! -d "$prefix"/"yugabyte-$ybversion_pg11" ]; then
  curl "https://downloads.yugabyte.com/releases/$ybversion_pg11/$ybfilename_pg11" \
      | tar xzv -C "$prefix"
fi

if [[ $OSTYPE = linux* ]]; then
  "$prefix/yugabyte-$ybversion_pg11/bin/post_install.sh"
fi
pushd "$prefix/yugabyte-$ybversion_pg11"
yb_ctl_destroy_create --rf=3

# Create pre-existing PG11 table
ysqlsh <<EOT
SHOW server_version;
CREATE TABLE t (h int, r TEXT, v1 BIGINT, v2 VARCHAR, v3 JSONB, v4 int[], PRIMARY KEY (h, r));
INSERT INTO t VALUES (1, 'a', 5000000000, 'abc', '{"a" : 3.5}', '{1, 2, 3}'),
(1, 'b', -5000000000, 'def', '{"b" : 5}', '{1, 1, 2, 3}'),
(2, 'a', 5000000000, 'ghi', '{"c" : 30}', '{1, 4, 9}');
SELECT * FROM t;
EOT
popd

# Upgrade to PG15 by first starting the masters in a mode that runs initdb in a mode that's aware of
# the PG11 to PG15 upgrade process, and then doing the actual upgrade flow (dump+restore).
# TODO: Run initdb and do the actual upgrade flow.

# Restart the masters as PG15 masters
for i in {1..3}; do
  yb_ctl restart_node $i --master \
      --master_flags="master_auto_run_initdb=true,TEST_online_pg11_to_pg15_upgrade=true"
done

# There is no code yet to run initdb on the PG15 catalogs, but PG11 tservers should still work.
# Ensure that the masters can still respond to PG11 queries.
ysqlsh <<EOT
SHOW server_version;
SELECT * FROM yb_servers();
SELECT * FROM pg_am;
SELECT * FROM t;
EOT
