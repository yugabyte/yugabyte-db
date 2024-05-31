---
title: Tablet splitting
headerTitle: Tablet splitting
linkTitle: Tablet splitting
description: Learn about tablet splitting mechanisms for resharding data, including presplitting, manual, and automatic.
menu:
  v2.16:
    identifier: docdb-tablet-splitting
    parent: architecture-docdb-sharding
    weight: 1143
type: docs
---

## Overview

Tablet splitting is the *resharding* of data in the cluster by presplitting tables before data is added or by changing the number of tablets at runtime. Currently, there are three mechanisms for tablet splitting in YugabyteDB clusters.

### Range scans

In use cases that scan a range of data, the data is stored in the natural sort order (also known as range-sharding). In these usage patterns, it is often impossible to predict a good split boundary ahead of time. For example:

```plpgsql
CREATE TABLE census_stats (
    age INTEGER,
    user_id INTEGER,
    ...
    );
```

In the preceding example, picking a good split point ahead of time is difficult. The database cannot infer the range of values for `age` (typically in the `1` to `100` range) in the table. And the distribution of rows in the table (that is, how many `user_id` rows will be inserted for each value of `age` to make an evenly distributed data split) is impossible to predict.

### Low-cardinality primary keys

In use cases with a low-cardinality of the primary keys or the secondary index, hashing is not effective. For example, if a table where the primary key or index is the column `gender`, with only two values (`Male` or `Female`), hash sharding would not be effective. Using the entire cluster of machines to maximize serving throughput, however, is important.

### Small tables that become very large

Some tables start off small, with only a few shards. However, if these tables grow very large, the nodes are continuously added to the cluster. Scenarios are possible where the number of nodes exceeds the number of tablets. To effectively rebalance the cluster, tablet splitting is required.

## Approaches to tablet splitting

DocDB allows data resharding by splitting tablets using the following three mechanisms:

* [Presplitting tablets](#presplitting-tablets): All tables created in DocDB can be split into the desired number of tablets at creation time.

* [Manual tablet splitting](#manual-tablet-splitting): The tablets in a running cluster can be split manually at runtime by you.

* [Automatic tablet splitting](#automatic-tablet-splitting): The tablets in a running cluster are automatically split according to some policy by the database.

## Presplitting tablets

At creation time, you can presplit a table into the desired number of tablets. YugabyteDB supports presplitting tablets for both range-sharded and hash-sharded YSQL tables, as well as hash-sharded YCQL tables. The number of tablets can be specified in one of the following two ways:

* Desired number of tablets: In this case, the table is created with the desired number of tablets.
* Desired number of tablets per node: In this case, the total number of tablets the table is split into is computed as follows:

    `num_tablets_in_table = num_tablets_per_node * num_nodes_at_table_creation_time`

In order to presplit a table into a desired number of tablets, you need the start key and end key for each tablet. This makes presplitting slightly different for hash-sharded and range-sharded tables.

### Hash-sharded tables

Because hash sharding works by applying a hash function on all or a subset of the primary key columns, the byte space of the hash sharded keys is known ahead of time. For example, if you use a 2-byte hash, the byte space would be `[0x0000, 0xFFFF]`.

![tablet_hash_1](/images/architecture/tablet_hash_1.png)

In the preceding diagram, the YSQL table is split into 16 shards. This can be achieved by using the `SPLIT INTO 16 TABLETS` clause as a part of the `CREATE TABLE` statement, as follows:

```plpgsql
CREATE TABLE customers (
    customer_id bpchar NOT NULL,
    company_name character varying(40) NOT NULL,
    contact_name character varying(30),
    contact_title character varying(30),
    address character varying(60),
    city character varying(15),
    region character varying(15),
    postal_code character varying(10),
    country character varying(15),
    phone character varying(24),
    fax character varying(24),
    PRIMARY KEY (customer_id HASH)
) SPLIT INTO 16 TABLETS;
```

For information on the relevant YSQL API, see the following:

* [CREATE TABLE ... SPLIT INTO](../../../api/ysql/the-sql-language/statements/ddl_create_table/#split-into) and the example, [Create a table specifying the number of tablets](../../../api/ysql/the-sql-language/statements/ddl_create_table/#create-a-table-specifying-the-number-of-tablets).
* [CREATE INDEX ... SPLIT INTO](../../../api/ysql/the-sql-language/statements/ddl_create_index/#split-into) and the example, [Create an index specifying the number of tablets](../../../api/ysql/the-sql-language/statements/ddl_create_index/#create-an-index-specifying-the-number-of-tablets).

For information on the relevant YCQL API, see the following:

* [CREATE TABLE ... WITH tablets](../../../api/ycql/ddl_create_table/#table-properties) and the example, [Create a table specifying the number of tablets](../../../api/ycql/ddl_create_table/#create-a-table-specifying-the-number-of-tablets).

### Range-sharded tables

In range-sharded YSQL tables, the start and end key for each tablet is not immediately known since this depends on the column type and the intended usage. For example, if the primary key is a `percentage NUMBER` column where the range of values is in the `[0, 100]` range, presplitting on the entire `NUMBER` space would not be effective.

For this reason, in order to presplit range-sharded tables, you must explicitly specify the split points. These explicit split points can be specified as follows:

```plpgsql
CREATE TABLE customers (
    customer_id bpchar NOT NULL,
    company_name character varying(40) NOT NULL,
    contact_name character varying(30),
    contact_title character varying(30),
    address character varying(60),
    city character varying(15),
    region character varying(15),
    postal_code character varying(10),
    country character varying(15),
    phone character varying(24),
    fax character varying(24),
    PRIMARY KEY (customer_id ASC)
) SPLIT AT VALUES ((1000), (2000), (3000), ... );
```

For information on the relevant YSQL API, see [CREATE TABLE ... SPLIT AT VALUES](../../../api/ysql/the-sql-language/statements/ddl_create_table/#split-at-values) for use with range-sharded tables.

## Manual tablet splitting

Imagine there is a table with pre-existing data spread across a certain number of tablets. It is possible to split some or all of the tablets in this table manually.

{{< note title="Note" >}}

Misuse or overuse of manual tablet splitting (for example, splitting tablets which get less traffic) may result in creation of a large number of tablets of different sizes and traffic rates. This cannot be easily remedied, as tablet merging is not currently supported.

{{< /note >}}

### Create a sample YSQL table

1. Create a three-node local cluster, as follows:

    ```sh
    ./bin/yb-ctl --rf=3 create --ysql_num_shards_per_tserver=1
    ```

1. Create a sample table and insert some data, as follows:

    ```plpgsql
    CREATE TABLE t (k VARCHAR, v TEXT, PRIMARY KEY (k)) SPLIT INTO 1 TABLETS;
    ```

1. Insert some sample data (100000 rows) into this table, as follows:

    ```plpgsql
    INSERT INTO t (k, v)
        SELECT i::text, left(md5(random()::text), 4)
        FROM generate_series(1, 100000) s(i);
    ```

    ```plpgsql
    SELECT count(*) FROM t;
    ```

    Expect the following output:

    ```output
    count
    --------
    100000
    (1 row)
    ```

### Verify the table has one tablet

In order to verify that the table `t` has only one tablet, list all the tablets for table `t` using the following [`yb-admin list_tablets`](../../../admin/yb-admin/#list-tablets) command:

```bash
./bin/yb-admin --master_addresses 127.0.0.1:7100 list_tablets ysql.yugabyte t
```

Expect the following output:

```output
Tablet UUID                       Range                         Leader
9991368c4b85456988303cd65a3c6503  key_start: "" key_end: ""     127.0.0.1:9100
```

Note the tablet UUID for later use.

### Manually flush the tablet

The tablet should have some data persisted on the disk. If you insert small amount of data, it can still exist in memory buffers only. To make sure SST data files exist on the disk, the tablet of this table can be manually flushed by running the following [`yb-ts-cli`](../../../admin/yb-ts-cli/#flush-tablet) command:

```sh
./bin/yb-ts-cli \
    --server_address=127.0.0.1:9100,127.0.0.2:9100,127.0.0.3:9100 \
    flush_tablet 9991368c4b85456988303cd65a3c6503
```

### Manually split the tablet

The tablet of this table can be manually split into two tablets by running the following [`yb-admin split_tablet`](../../../admin/yb-admin/#split-tablet) command:

```sh
./bin/yb-admin \
    --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    split_tablet 9991368c4b85456988303cd65a3c6503
```

After the split, you expect to see two tablets for the table `t`, as per the following output:

```output
Tablet UUID                       Range                                 Leader
20998f68c3fa4d299e8af7c04410e230  key_start: "" key_end: "\177\377"     127.0.0.1:9100
a89ecb84ad1b488b893b6e7762a6ca2a  key_start: "\177\377" key_end: ""     127.0.0.3:9100
```

The original tablet `9991368c4b85456988303cd65a3c6503` no longer exists; it has been replaced with two new tablets.

The tablet leaders are now spread across two nodes in order to evenly balance the tablets for the table across the nodes of the cluster.

## Automatic tablet splitting

Automatic tablet splitting enables resharding of data in a cluster automatically while online, as well as transparently, when a specified size threshold has been reached.

For details on the architecture design, see [Automatic re-sharding of data with tablet splitting](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/docdb-automatic-tablet-splitting.md).

### Enable automatic tablet splitting

When automatic tablet splitting is enabled, newly-created tables with [hash sharding](../../../architecture/docdb-sharding/sharding/#hash-sharding) have one tablet per node by default.

In addition, from version 2.16.5, for servers with up to 2 CPU cores, newly-created tables have one tablet per cluster, and servers with up to 4 CPU cores have 2 tablets per cluster.

From version 2.18.0, automatic tablet splitting is turned on by default.

To control automatic tablet splitting, use the `yb-master` [`--enable_automatic_tablet_splitting`](../../../reference/configuration/yb-master/#enable-automatic-tablet-splitting) flag and specify the associated flags to configure when tablets should split, and use `yb-tserver` [`--enable_automatic_tablet_splitting`](../../../reference/configuration/yb-tserver/#enable-automatic-tablet-splitting). The flags must match on all `yb-master` and `yb-tserver` configurations of a YugabyteDB cluster.

{{< note title="Note" >}}

Newly-created tables with [range sharding](../../../architecture/docdb-sharding/sharding/#range-sharding) always have one shard *per cluster* unless table partitioning is specified explicitly during table creation.

{{< /note >}}

Automatic tablet splitting happens in three phases, determined by the shard count per node. As the shard count increases, the threshold size for splitting a tablet also increases, as follows:

#### Low Phase

In the low phase, each node has fewer than [`tablet_split_low_phase_shard_count_per_node`](../../../reference/configuration/yb-master/#tablet-split-low-phase-shard-count-per-node) shards (8 by default). In this phase, YugabyteDB splits tablets larger than [`tablet_split_low_phase_size_threshold_bytes`](../../../reference/configuration/yb-master/#tablet-split-low-phase-size-threshold-bytes) (512 MB by default).

#### High Phase

In the high phase, each node has fewer than [`tablet_split_high_phase_shard_count_per_node`](../../../reference/configuration/yb-master/#tablet-split-high-phase-shard-count-per-node) shards (24 by default). In this phase, YugabyteDB splits tablets larger than [`tablet_split_high_phase_size_threshold_bytes`](../../../reference/configuration/yb-master/#tablet-split-high-phase-size-threshold-bytes) (10 GB by default).

#### Final Phase

When the shard count exceeds the high phase count (determined by `tablet_split_high_phase_shard_count_per_node`, 24 by default), YugabyteDB splits tablets larger than [`tablet_force_split_threshold_bytes`](../../../reference/configuration/yb-master/#tablet-force-split-threshold-bytes) (100 GB by default). This will continue until we reach the [`tablet_split_limit_per_table`](../../../reference/configuration/yb-master/#tablet-split-limit-per-table) tablets per table limit (256 tablets by default; if set to 0, there will be no limit).

### Post-split compactions

Once a split has been executed on a tablet, the two resulting tablets will require a full compaction in order to remove unnecessary data from each. These post-split compactions can significantly increase CPU and disk usage and thus impact performance. To limit this impact, the number of simultaneous tablet splits allowed is conservative by default.

In the event that performance suffers due to automatic tablet splitting, the following flags can be used for tuning:

[Master flags](../../../reference/configuration/yb-master/#tablet-splitting-flags)

* `outstanding_tablet_split_limit` limits the total number of outstanding tablet splits to 1 by default. Tablets that are performing post-split compactions count against this limit.
* `outstanding_tablet_split_limit_per_tserver` limits the total number of outstanding tablet splits per node to 1 by default. Tablets that are performing post-split compactions count against this limit.

[TServer flags](../../../reference/configuration/yb-tserver/#sharding-flags)

* `post_split_trigger_compaction_pool_max_threads` indicates the number of threads dedicated to post-split compaction tasks per node. By default, this is limited to 1. Increasing this may complete tablet splits faster, but will require more CPU and disk resources.
* `post_split_trigger_compaction_pool_max_queue_size` indicates the number of outstanding post-split compaction tasks that can be queued at once per node, limited to 16 by default.
* `automatic_compaction_extra_priority` provides additional compaction priorities to [smaller compactions](../../concepts/yb-tserver/#small-and-large-compaction-queues) when automatic tablet splitting is enabled. This prevents smaller compactions from being starved for resources by the larger post-split compactions. This is set to 50 by default (the maximum recommended), and can be reduced to 0.

### Example: YCSB workload with automatic tablet splitting

In the following example, a three-node cluster is created and uses a YCSB workload to demonstrate the use of automatic tablet splitting in a YSQL database:

1. Create a three-node cluster, as follows:

    ```sh
    ./bin/yb-ctl --rf=3 create --master_flags "enable_automatic_tablet_splitting=true,tablet_split_low_phase_size_threshold_bytes=30000000" --tserver_flags "memstore_size_mb=10"
    ```

1. Create a table for workload, as follows:

    ```sh
    ./bin/ysqlsh -c "CREATE DATABASE ycsb;"
    ./bin/ysqlsh -d ycsb -c "CREATE TABLE usertable (
            YCSB_KEY TEXT,
            FIELD0 TEXT, FIELD1 TEXT, FIELD2 TEXT, FIELD3 TEXT,
            FIELD4 TEXT, FIELD5 TEXT, FIELD6 TEXT, FIELD7 TEXT,
            FIELD8 TEXT, FIELD9 TEXT,
            PRIMARY KEY (YCSB_KEY ASC));"
    ```

1. Create the properties file for YCSB at `~/code/YCSB/db-local.properties` and add the following content:

    ```conf
    db.driver=org.postgresql.Driver
    db.url=jdbc:postgresql://127.0.0.1:5433/ycsb;jdbc:postgresql://127.0.0.2:5433/ycsb;jdbc:postgresql://127.0.0.3:5433/ycsb
    db.user=yugabyte
    db.passwd=
    core_workload_insertion_retry_limit=10
    ```

1. Start loading data for the workload, as follows:

    ```sh
    ~/code/YCSB/bin/ycsb load jdbc -s -P ~/code/YCSB/db-local.properties -P ~/code/YCSB/workloads/workloada -p recordcount=500000 -p operationcount=1000000 -p threadcount=4
    ```

1. Monitor the tablet splitting using the YugabyteDB web interfaces at `http://localhost:7000/tablet-servers` and `http://127.0.0.1:9000/tablets`.

1. Run the workload, as follows:

    ```sh
    ~/code/YCSB/bin/ycsb run jdbc -s -P ~/code/YCSB/db-local.properties -P ~/code/YCSB/workloads/workloada -p recordcount=500000 -p operationcount=1000000 -p threadcount=4
    ```

1. Get the list of tablets accessed during the run phase of the workload, as follows:

    ```sh
    diff -C1 after-load.json after-run.json | grep tablet_id | sort | uniq
    ```

1. The list of tablets accessed can be compared with `http://127.0.0.1:9000/tablets` to make sure no presplit tablets have been accessed.

 For details on using YCSB with YugabyteDB, see [Benchmark: YCSB](../../../benchmark/ycsb-jdbc/).

## Limitations

* Tablet splitting is disabled for index tables with range partitioning that are being restored in version 2.14.5 or later from a backup taken in any version prior to 2.14.5. It is not recommended to use tablet splitting for range partitioned index tables prior to version 2.14.5 to prevent possible data loss for index tables. For details, see [#12190](https://github.com/yugabyte/yugabyte-db/issues/12190) and [#17169](https://github.com/yugabyte/yugabyte-db/issues/17169).

The following known limitations are planned to be resolved in upcoming releases:

* Colocated tables cannot be split. For details, see [#4463](https://github.com/yugabyte/yugabyte-db/issues/4463).
* In 2.14.0, when tablet splitting is used with Point In Time Recovery (PITR), restoring to arbitrary times in the past when a tablet is in the process of splitting is not supported. This is fixed in 2.14.1.
* Tablet splitting is currently disabled by default for tables with cross cluster replication. It can be enabled using the [`enable_tablet_split_of_xcluster_replicated_tables`](../../../reference/configuration/yb-master/#enable-tablet-split-of-xcluster-replicated-tables) flag on master on both the producer and consumer clusters (as they will perform splits independently of one another). If using this feature after upgrade, the producer and consumer clusters should both be upgraded to 2.14.0+ before enabling the feature.
* Tablet splitting is currently disabled during bootstrap for tables with cross cluster replication. For details, see [#13170](https://github.com/yugabyte/yugabyte-db/issues/13170).
* Tablet splitting is currently disabled for tables that are using the [TTL file expiration](../../../develop/learn/ttl-data-expiration-ycql/#efficient-data-expiration-for-ttl) feature.

To follow the tablet splitting work-in-progress, see [#1004](https://github.com/yugabyte/yugabyte-db/issues/1004).
