---
title: Tablet splitting
headerTitle: Tablet splitting
linkTitle: Tablet splitting
description: Learn about tablet splitting mechanisms for resharding data, including presplitting, manual, and automatic.
menu:
  v2024.1:
    identifier: docdb-tablet-splitting
    parent: architecture-docdb-sharding
    weight: 1143
type: docs
---

## Overview

Tablet splitting is the resharding of data in the universe by presplitting tables before data is added or by changing the number of tablets at runtime. Currently, there are [three mechanisms for tablet splitting](#approaches-to-tablet-splitting) in YugabyteDB universes.

### Range scans

When there is a need to scan a range of data, the data is stored in the natural sort order (also known as range-sharding). In such cases, it is often impossible to predict a good split boundary ahead of time. Consider the following example:

```plpgsql
CREATE TABLE census_stats (
    age INTEGER,
    user_id INTEGER,
    ...
    );
```

In this example, picking a split point ahead of time is difficult. The database cannot infer the range of values for `age` (typically in the `1` to `100` range) in the table. And the distribution of rows in the table (that is, how many `user_id` rows will be inserted for each value of `age` to make an evenly distributed data split) is impossible to predict.

### Low-cardinality primary keys

In use cases with a low cardinality of the primary keys or the secondary index, hashing is not effective. For example, if a table where the primary key or index is the column `gender`, with only two values (`Male` or `Female`), hash sharding would not be effective. Using the entire cluster of machines to maximize serving throughput, however, is important.

### Small tables that become very large

Some tables start off small, with only a few shards. If these tables grow very large, the nodes are continuously added to the universe. It is possible that the number of nodes exceeds the number of tablets. To effectively rebalance the cluster, tablet splitting is required.

## Approaches to tablet splitting

DocDB allows data resharding by splitting tablets using the following three mechanisms:

* [Presplitting tablets](#presplitting-tablets): All tables created in DocDB can be split into the desired number of tablets at creation time.

* [Manual tablet splitting](#manual-tablet-splitting): The tablets in a running cluster can be split manually at runtime by you.

* [Automatic tablet splitting](#automatic-tablet-splitting): The tablets in a running cluster are automatically split according to some policy by the database.

### Presplitting tablets

At creation time, you can presplit a table into the desired number of tablets. YugabyteDB supports presplitting tablets for both range-sharded and hash-sharded YSQL tables, as well as hash-sharded YCQL tables. The number of tablets can be specified in one of the following two ways:

* Desired number of tablets, when the table is created with the desired number of tablets.
* Desired number of tablets per node, when the total number of tablets the table is split into is computed as follows:

    `num_tablets_in_table = num_tablets_per_node * num_nodes_at_table_creation_time`

To presplit a table into a desired number of tablets, you need the start key and end key for each tablet. This makes presplitting slightly different for hash-sharded and range-sharded tables.

The maximum number of tablets allowed at table creation time is controlled by [`max_create_tablets_per_ts`](../../../reference/configuration/yb-master/#max-create-tablets-per-ts). This also limits the number of tablets that can be created by tablet splitting.

#### Hash-sharded tables

Because hash sharding works by applying a hash function on all or a subset of the primary key columns, the byte space of the hash sharded keys is known ahead of time. For example, if you use a 2-byte hash, the byte space would be `[0x0000, 0xFFFF]`, as per the following illustration:

![tablet_hash_1](/images/architecture/tablet_hash_1.png)

In the preceding diagram, the YSQL table is split into sixteen shards. This can be achieved by using the `SPLIT INTO 16 TABLETS` clause as a part of the `CREATE TABLE` statement, as follows:

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

#### Range-sharded tables

In range-sharded YSQL tables, the start and end key for each tablet is not immediately known as this depends on the column type and the intended usage. For example, if the primary key is a `percentage NUMBER` column where the range of values is in the `[0, 100]` range, presplitting on the entire `NUMBER` space would not be effective.

For this reason, in order to presplit range-sharded tables, you must explicitly specify the split points, as per the following example:

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

### Manual tablet splitting

Suppose there is a table with pre-existing data spread across a certain number of tablets. It is possible to split some or all of the tablets in this table manually.

Note that misuse or overuse of manual tablet splitting (for example, splitting tablets which get less traffic) may result in creation of a large number of tablets of different sizes and traffic rates. This cannot be easily remedied, as tablet merging is not currently supported.

#### Create a sample YSQL table

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

#### Verify the table has one tablet

To verify that the table `t` has only one tablet, list all the tablets for table `t` using the following [`yb-admin list_tablets`](../../../admin/yb-admin/#list-tablets) command:

```bash
./bin/yb-admin --master_addresses 127.0.0.1:7100 list_tablets ysql.yugabyte t
```

Expect the following output:

```output
Tablet UUID                       Range                         Leader
9991368c4b85456988303cd65a3c6503  key_start: "" key_end: ""     127.0.0.1:9100
```

Note the tablet UUID for later use.

#### Manually flush the tablet

The tablet should have some data persisted on the disk. If you insert small amount of data, it can still exist in memory buffers only. To make sure SST data files exist on the disk, the tablet of this table can be manually flushed by running the following [`yb-ts-cli`](../../../admin/yb-ts-cli/#flush-tablet) command:

```sh
./bin/yb-ts-cli \
    --server_address=127.0.0.1:9100,127.0.0.2:9100,127.0.0.3:9100 \
    flush_tablet 9991368c4b85456988303cd65a3c6503
```

#### Manually split the tablet

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

### Automatic tablet splitting

Automatic tablet splitting enables resharding of data in a cluster automatically while online, as well as transparently, when a specified size threshold has been reached.

For details on the architecture design, see [Automatic resharding of data with tablet splitting](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/docdb-automatic-tablet-splitting.md).

#### Enable automatic tablet splitting

When automatic tablet splitting is enabled, newly-created tables with [hash sharding](../../../architecture/docdb-sharding/sharding/#hash-sharding) have one tablet per node by default.

In addition, from version 2.14.10, for servers with up to 2 CPU cores, newly-created tables have one tablet per cluster, and servers with up to 4 CPU cores have 2 tablets per cluster.

From version 2.18.0, automatic tablet splitting is turned on by default.

To adjust the behavior of automatic tablet splitting, set the relevant [tablet-splitting-flags](../../../reference/configuration/yb-master/#tablet-splitting-flags).

{{< note title="Note" >}}

Newly-created tables with [range sharding](../../../architecture/docdb-sharding/sharding/#range-sharding) always have one tablet _per cluster_ unless table partitioning is specified explicitly during table creation.

{{< /note >}}

Automatic tablet splitting happens in three phases, determined by the shard count per node. As the shard count increases, the threshold size for splitting a tablet also increases, as follows:

##### Low phase

In the low phase, each node has fewer than [`tablet_split_low_phase_shard_count_per_node`](../../../reference/configuration/yb-master/#tablet-split-low-phase-shard-count-per-node) shards (1 by default). In this phase, YugabyteDB splits tablets larger than [`tablet_split_low_phase_size_threshold_bytes`](../../../reference/configuration/yb-master/#tablet-split-low-phase-size-threshold-bytes) (128 MiB by default).

##### High phase

In the high phase, each node has fewer than [`tablet_split_high_phase_shard_count_per_node`](../../../reference/configuration/yb-master/#tablet-split-high-phase-shard-count-per-node) shards (24 by default). In this phase, YugabyteDB splits tablets larger than [`tablet_split_high_phase_size_threshold_bytes`](../../../reference/configuration/yb-master/#tablet-split-high-phase-size-threshold-bytes) (10 GiB by default).

##### Final phase

When the shard count exceeds the high phase count (determined by `tablet_split_high_phase_shard_count_per_node`, 24 by default), YugabyteDB splits tablets larger than [`tablet_force_split_threshold_bytes`](../../../reference/configuration/yb-master/#tablet-force-split-threshold-bytes) (100 GiB by default). The maximum number of tablets is still limited by [`max_create_tablets_per_ts`](../../../reference/configuration/yb-master/#max-create-tablets-per-ts).

#### Post-split compactions

Once a split has been executed on a tablet, the two resulting tablets require a full compaction in order to remove unnecessary data from each. These post-split compactions can significantly increase CPU and disk usage and thus impact performance. To limit the impact, the number of simultaneous tablet splits allowed is conservative by default.

In the event that performance suffers due to automatic tablet splitting, the following flags can be used for tuning:

* [YB-Master flags](../../../reference/configuration/yb-master/#tablet-splitting-flags):
  * `outstanding_tablet_split_limit` limits the total number of outstanding tablet splits to 1 by default. Tablets that are performing post-split compactions count against this limit.
  * `outstanding_tablet_split_limit_per_tserver` limits the total number of outstanding tablet splits per node to 1 by default. Tablets that are performing post-split compactions count against this limit.
* [YB-TServer flags](../../../reference/configuration/yb-tserver/#sharding-flags):
  * `post_split_trigger_compaction_pool_max_threads` indicates the number of threads dedicated to post-split compaction tasks per node. By default, this is limited to 1. Increasing this may complete tablet splits faster, but would require more CPU and disk resources.
  * `post_split_trigger_compaction_pool_max_queue_size` indicates the number of outstanding post-split compaction tasks that can be queued at once per node, limited to 16 by default.
  * `automatic_compaction_extra_priority` provides additional compaction priorities to [smaller compactions](../../yb-tserver/#compaction-queues) when automatic tablet splitting is enabled. This prevents smaller compactions from being starved for resources by the larger post-split compactions. This is set to 50 by default (the maximum recommended), and can be reduced to 0.

#### YCSB workload with automatic tablet splitting example

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

## Tablet limits

This feature adds a configurable limit to the total number of tablet replicas that a cluster can support. If you try to create a table whose additional tablet replicas would bring the total number of tablet replicas in the cluster over this limit, the create table request is rejected.

### Configure

You can enable tablet limits using the following steps:

1. Enable the flag [enforce_tablet_replica_limits](../../../reference/configuration/yb-master/#enforce-tablet-replica-limits) on all YB-Masters.
1. Choose a resource to limit the total number of tablet replicas. Limits are supported by available CPU cores, GiB of RAM, or both.
   * To limit by memory, if you're using YSQL, it is simplest to set the flag [use_memory_defaults_optimized_for_ysql](../../../reference/configuration/yb-tserver/#use-memory-defaults-optimized-for-ysql) to true.
   * To limit by CPU cores and GiB, or both, set the flags [tablet_replicas_per_core_limit](../../../reference/configuration/yb-tserver/#tablet-replicas-per-core-limit) and [tablet_replicas_per_gib_limit](../../../reference/configuration/yb-tserver/#tablet-replicas-per-gib-limit) to the desired positive value on all YB-Masters and YB-TServers.

     These flags limit the number of tablets that can be created in the live placement group in terms of resources available to YB-TServers in the cluster. For example, if [tablet_overhead_size_percentage](../../../reference/configuration/yb-tserver/#tablet-overhead-size-percentage) is 10, each YB-TServer has 10 GiB available, `tablet_replicas_per_gib_limit` is 1000, and there are 3 YB-TServers in the cluster, this feature will prevent you from creating more than 3000 tablet replicas. Assuming a replication factor of 3, this is the same as 1000 tablets. Note that YugabyteDB creates a certain number of system tablets itself, so in this case you are not free to create 1000 tablets.

1. Optionally, set the flag [split_respects_tablet_replica_limits](../../../reference/configuration/yb-master/#split-respects-tablet-replica-limits) to true on all YB-Masters to block tablet splits when the configured limits are reached.

{{< tip title="Tip" >}}

To view the number of live tablets and the limits, open the **YB-Master UI** (`<master_host>:7000/`) and click the **Universe Summary** section. The total number of live tablet replicas is listed under "Active Tablet-Peers", and the limit is listed under "Tablet Peer Limit".
{{< /tip >}}

To disable the feature, set the flag `enforce_tablet_replica_limits` to false on all YB-Masters.

{{<note title="YSQL restores are not fully supported">}}

Currently, tablets created during a YSQL restore are not entirely covered by this feature. The YSQL restore flow creates tablets in two steps:

1. The restored tables are created by executing SQL DDLs.
1. If necessary, new tablets are created so the number of tablets supporting the restored tables matches the number of tablets supporting the backed up tables. Any tablets created during this step are _not_ checked against the cluster limits.

{{</note>}}

### Metrics

The following table describes metrics related to tablet limits.

| Metric | Description |
| :----- | :---------- |
| ts_supportable_tablet_peers | The number of tablet replicas this TServer can support. -1 if there is no limit. |
| create_table_too_many_tablets | The number of create table requests failed because the cluster cannot support the additional tablet replicas. |
| split_tablet_too_many_tablets | The number of split tablet operations failed because the cluster cannot support the additional tablet replicas. |

### Example

{{% explore-setup-single-local %}}

Assuming a cluster has been properly configured, if you try to create a table beyond the configurable limit, the error message you can expect to see in a ysqlsh session is as follows:

```sql
create table foo (i int primary key, j int) split into 10 tablets;
```

```output
ERROR:  Invalid table definition: Error creating table yugabyte.foo on the master: The requested number of tablet replicas (30) would cause the total running tablet replica count (102) to exceed the safe system maximum (93)
```

### Best practices

YugabyteDB has pre-computed sensible defaults for the memory limiting flags. By setting the [use_memory_defaults_optimized_for_ysql](../../../reference/configuration/yb-tserver/#use-memory-defaults-optimized-for-ysql) flag to true, the [tablet_overhead_size_percentage](../../../reference/configuration/yb-tserver/#tablet-overhead-size-percentage) is set with a sensible default as well. So, to use the memory limits, the only flag that needs to be set is [enforce_tablet_replica_limits](../../../reference/configuration/yb-master/#enforce-tablet-replica-limits).

It is recommended to use the pre-computed memory defaults. Setting `use_memory_defaults_optimized_for_ysql` reserves memory for PostgreSQL, which is wasteful if not using YSQL. In this case, set the `tablet_overhead_size_percentage` flag to 10 on all YB-TServers.

## Limitations

* Tablet splitting is disabled for index tables with range partitioning that are being restored in version 2.14.5 or later from a backup taken in any version prior to 2.14.5. It is not recommended to use tablet splitting for range partitioned index tables prior to version 2.14.5 to prevent possible data loss for index tables. For details, see [#12190](https://github.com/yugabyte/yugabyte-db/issues/12190) and [#17169](https://github.com/yugabyte/yugabyte-db/issues/17169).

The following known limitations are planned to be resolved in upcoming releases:

* Colocated tables cannot be split. For details, see [#4463](https://github.com/yugabyte/yugabyte-db/issues/4463).
* In YugabyteDB version 2.14.0, when tablet splitting is used with point in time recovery (PITR), restoring to arbitrary times in the past when a tablet is in the process of splitting is not supported. This was resolved in 2.14.1.
* Tablet splitting is currently disabled during bootstrap for tables with cross-cluster replication. For details, see [#13170](https://github.com/yugabyte/yugabyte-db/issues/13170).
* Tablet splitting is currently disabled for tables that are using the [TTL file expiration](../../../develop/learn/ttl-data-expiration-ycql/#efficient-data-expiration-for-ttl) feature.

To follow the tablet splitting work-in-progress, see [#1004](https://github.com/yugabyte/yugabyte-db/issues/1004).
