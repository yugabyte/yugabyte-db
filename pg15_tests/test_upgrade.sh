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

# Store PG DB OIDs for later comparison
pghost2=127.0.0.$((ip_start + 1))
pghost3=127.0.0.$((ip_start + 2))
bin/yb-admin --master_addresses=$PGHOST:7100,$pghost2:7100,$pghost3:7100 list_namespaces \
  | grep 30008 | sort -k2 | awk '{print $1 " " $2}' > $data_dir/pg11_dbs.txt

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

# Restart the masters as PG15 masters
for i in {1..3}; do
  yb_ctl restart_node $i --master \
      --master_flags="master_auto_run_initdb=true,TEST_online_pg11_to_pg15_upgrade=true"
done

# Wait until initdb has finished. On a Mac, it takes around 22 seconds on a release build, or over
# 90 seconds on debug. initdb typically starts on node 1, but we monitor all nodes in case it starts
# on another one.
echo initdb starting at $(date +"%r")
timeout 120 bash -c "tail -F $data_dir/node-1/disk-1/yb-data/master/logs/yb-master.INFO \
                             $data_dir/node-2/disk-1/yb-data/master/logs/yb-master.INFO \
                             $data_dir/node-3/disk-1/yb-data/master/logs/yb-master.INFO | \
    grep -m 1 \"initdb completed successfully\""

# Ensure that the PG15 initdb didn't create or modify namespace entries on the YB master.
diff $data_dir/pg11_dbs.txt <(build/latest/bin/yb-admin \
  --master_addresses=$PGHOST:7100,$pghost2:7100,$pghost3:7100 list_namespaces | grep 30008 \
  | sort -k2 | awk '{print $1 " " $2}')

# Restart tserver 2 to PG15 for the upgrade, with postgres binaries in binary_upgrade mode
yb_ctl restart_node 2 --tserver_flags="TEST_pg_binary_upgrade=true"

# To simulate a core piece of pg_upgrade, we run ysql_dump and pg_restore. Together these utilities
# with these options migrate the metadata for database 'yugabyte'.
build/latest/postgres/bin/ysql_dump -h "$PGHOST" --schema-only --quote-all-identifiers \
    --binary-upgrade --format=custom --file="$data_dir"/yugabyte-db.custom \
    --include-yb-metadata dbname=yugabyte
build/latest/postgres/bin/pg_restore -h "$pghost2" --clean --create --exit-on-error --verbose \
    --dbname template1 "$data_dir"/yugabyte-db.custom

# The upgrade is finished. Restart node 2 with postgres binaries *not* in binary upgrade mode
yb_ctl restart_node 2

# Demonstrate simultaneous access for DMLs before the upgrade has been finalized. (DDLs are not
# allowed, and rollback to PG11 is still possible.)
# YB_TODO: Test that DDLs are prohibited when the functionality is implemented.

# Insert from PG15
diff <(ysqlsh 2 <<EOT
SHOW server_version_num;
INSERT INTO t VALUES (15, 'fifteen', cosh(0), '15', '{"num" : 15}', '{15}');
SELECT * FROM t ORDER BY h,r;
EOT
) - <<EOT
 server_version_num 
--------------------
 150002
(1 row)

INSERT 0 1
 h  |    r    |     v1      | v2  |     v3      |    v4     
----+---------+-------------+-----+-------------+-----------
  1 | a       |  5000000000 | abc | {"a": 3.5}  | {1,2,3}
  1 | b       | -5000000000 | def | {"b": 5}    | {1,1,2,3}
  2 | a       |  5000000000 | ghi | {"c": 30}   | {1,4,9}
 15 | fifteen |           1 | 15  | {"num": 15} | {15}
(4 rows)

EOT
# Insert from PG11, and note the PG15 insertion is visible
diff <(ysqlsh <<EOT
SHOW server_version_num;
INSERT INTO t VALUES (11, 'eleven', 11, '11', '{"num": 11}', '{11}');
SELECT * FROM t ORDER BY h,r;
EOT
) - <<EOT
 server_version_num 
--------------------
 110002
(1 row)

INSERT 0 1
 h  |    r    |     v1      | v2  |     v3      |    v4     
----+---------+-------------+-----+-------------+-----------
  1 | a       |  5000000000 | abc | {"a": 3.5}  | {1,2,3}
  1 | b       | -5000000000 | def | {"b": 5}    | {1,1,2,3}
  2 | a       |  5000000000 | ghi | {"c": 30}   | {1,4,9}
 11 | eleven  |          11 | 11  | {"num": 11} | {11}
 15 | fifteen |           1 | 15  | {"num": 15} | {15}
(5 rows)

EOT

# Upgrade is complete. After the restart, demonstrate that DDLs work.
yb_ctl restart
diff <(ysqlsh <<EOT
SHOW server_version_num;
SELECT * FROM t ORDER BY h,r;
CREATE INDEX ON t (v1);
EXPLAIN (COSTS OFF) SELECT COUNT(*) FROM t WHERE v1 = 11;
SELECT COUNT(*) FROM t WHERE v1 = 11;
EOT
) - <<EOT
 server_version_num 
--------------------
 150002
(1 row)

 h  |    r    |     v1      | v2  |     v3      |    v4     
----+---------+-------------+-----+-------------+-----------
  1 | a       |  5000000000 | abc | {"a": 3.5}  | {1,2,3}
  1 | b       | -5000000000 | def | {"b": 5}    | {1,1,2,3}
  2 | a       |  5000000000 | ghi | {"c": 30}   | {1,4,9}
 11 | eleven  |          11 | 11  | {"num": 11} | {11}
 15 | fifteen |           1 | 15  | {"num": 15} | {15}
(5 rows)

CREATE INDEX
                QUERY PLAN                 
-------------------------------------------
 Aggregate
   ->  Index Only Scan using t_v1_idx on t
         Index Cond: (v1 = 11)
(3 rows)

 count 
-------
     1
(1 row)

EOT
