---
title: Tablet splitting
headerTitle: Tablet splitting
linkTitle: Tablet splitting
description: Learn about tablet splitting mechanisms for resharding data, including pre-splitting, manual, and automatic.
menu:
  latest:
    identifier: docdb-tablet-splitting
    parent: architecture-docdb-sharding
    weight: 1143
isTocNested: true
showAsideToc: true
---

Tablet splitting enables changing the number of tablets at runtime. This enables *resharding* the existing data in the cluster.

## Overview

There are a number of scenarios where this is useful.

### Range scans

In use cases that scan a range of data, the data is stored in the natural sort order (also known as range-sharding). In these usage patterns, it is often impossible to predict a good split boundary ahead of time. For example:

```
CREATE TABLE census_stats (
    age INTEGER,
    user_id INTEGER,
    ...
    );
```

In the example above, picking a good split point ahead of time is difficult. The database cannot infer the range of values for `age` (typically in the `1` to `100` range) in the table. And the distribution of rows in the table (that is, how many `user_id` rows will be inserted for each value of `age` to make an evenly distributed data split) is impossible to predict.

### Low-cardinality primary keys

In use cases with a low-cardinality of the primary keys (or the secondary index), hashing is not effective. For example, if a table where the primary key (or index) is the column `gender`, with only two values (`Male` or `Female`), hash sharding would not be effective. Using the entire cluster of machines to maximize serving throughput, however, is important.

### Small tables that become very large

Some tables start off small, with only a few shards. But, if these tables grow very large, then nodes get added to the cluster continuously.  Scenarios are possible that the number of nodes exceeds the number of tablets. To effectively rebalance the cluster, tablet splitting is required.

## Approaches to tablet splitting

DocDB allows reshard data by splitting tablets using the following three mechanisms:

* **Pre-splitting tablets:** All tables created in DocDB can be split into the desired number of tablets at creation time.

* **Manual tablet splitting:** The tablets in a running cluster can be split manually at runtime by the user.

* **Automatic tablet splitting:** The tablets in a running cluster are automatically split according to some policy by the database.

The following sections give details on how to split tablets using these three approaches.

## Pre-splitting tablets

At creation time, you can pre-split a table split into the desired number of tablets. YugabyteDB supports pre-splitting tablets for both *range-sharded* and *hash-sharded* tables. The number of tablets can be specified in one of two ways:

* **Desired number of tablets:** In this case, the table is created with the desired number of tablets.
* **Desired number of tablets per node:** In this case, the total number of tablets the table is split into is computed as follows:

    ```
    num_tablets_in_table = num_tablets_per_node * num_nodes_at_table_creation_time
    ```

{{< note title="Note" >}}

In order to pre-split a table into a desired number of tablets, you need the start key and end key for each tablet. This makes pre-splitting slightly different for hash-sharded and range-sharded tables.

{{</note >}}

### Hash-sharded tables

 Because hash sharding works by applying a hash function on all, or a subset of, the primary key columns, the byte space of the hash sharded keys is known ahead of time. For example, if you use a 2-byte hash, the byte space would be `[0x0000, 0xFFFF]`.

![tablet_hash_1](/images/architecture/tablet_hash_1.png)

In the diagram above, the YSQL table is split into 16 shards. This can be achieved by using the `SPLIT INTO 16 TABLETS` clause as a part of the `CREATE TABLE` statement, as shown below.

```
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

* For YSQL API details, see [Create a table specifying the number of tablets](../../../api/ysql/commands/ddl_create_table/#create-a-table-specifying-the-number-of-tablets). 
* For YCQL API details, see [YCQL `CREATE TABLE`](../../../api/ycql/ddl_create_table/#create-a-table-specifying-the-number-of-tablets).

### Range-sharded tables

In range-sharded tables, the start and end key for each tablet is not immediately known since this depends on the column type and the intended usage. For example, if the primary key is a `percentage NUMBER` column where the range of values is in the `[0, 100]` range, pre-splitting on the entire `NUMBER` space would not be effective.

For this reason, in order to pre-split range sharded tables, it is necessary to explicitly specify the split points. These explicit split points can be specified as shown below.

```
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
) SPLIT AT ((1000), (2000), (3000), ... );
```

## Manual tablet splitting

Imagine there is a table with pre-existing data spread across a certain number of tablets. It is possible to split some or all of the tablets in this table manually. This is shown in the example below.

### Create a sample YSQL table

First, create a three-node local cluster.

```bash
$ bin/yb-ctl --rf=3 create --num_shards_per_tserver=1
```

Create a sample table and insert some data.

```postgres
yugabyte=# CREATE TABLE t (k VARCHAR, v TEXT, PRIMARY KEY (k)) SPLIT INTO 1 TABLETS;
```

Next, we insert some sample data (100K rows) into this table.

```postgres
yugabyte=# INSERT INTO t (k, v) 
             SELECT i::text, left(md5(random()::text), 4)
             FROM generate_series(1, 100000) s(i);
```

```
yugabyte=# select count(*) from t;
 count
--------
 100000
(1 row)

```

### Verify a table has one tablet

In order to verify that the table `t` has only one tablet, list all the tablets for table `t` using the following [`yb-admin list_tablets`](../../admin/yb-admin/#list-tablets) command.

```bash
$ bin/yb-admin --master_addresses 127.0.0.1:7100 list_tablets ysql.yugabyte t
```

This produces the following output. Note the tablet UUID for use later to split this tablet.

```
Tablet UUID                       Range                         Leader
9991368c4b85456988303cd65a3c6503  key_start: "" key_end: ""     127.0.0.1:9100
```

### Manually split the tablet

The tablet of this table can be manually split into two tablets by running the following [`yb-admin split_tablet`](../../admin/yb-admin/split-tablet) command.

```sh
$ bin/yb-admin \
    --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    split_tablet 9991368c4b85456988303cd65a3c6503
```

After the split, you see two tablets for the table `t`.

```sh
$ bin/yb-admin --master_addresses 127.0.0.1:7100 list_tablets ysql.yugabyte t
```

```
Tablet UUID                       Range                                 Leader
20998f68c3fa4d299e8af7c04410e230  key_start: "" key_end: "\177\377"     127.0.0.1:9100
a89ecb84ad1b488b893b6e7762a6ca2a  key_start: "\177\377" key_end: ""     127.0.0.3:9100
```

{{< note title="Important" >}}

* The original tablet `9991368c4b85456988303cd65a3c6503` is no longer exists and has been replaced with two new tablets.
* The tablet leaders are now spread across two nodes in order to evenly balance the tablets for the table across the nodes of the cluster.

{{</note >}}

## Automatic tablet splitting

{{< note title="Note" >}}

Automatic tablet splitting is currently in [BETA](../../../../faq/general/#what-is-the-definition-of-the-beta-feature-tag).

{{< /note >}}

Automatic tablet splitting enables resharding of the data in a cluster automatically while online, and transparently to users, when a specified size threshold has been reached.

Design for automatic tablet splitting can be reviewed on [GitHub](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/docdb-automatic-tablet-splitting.md). While the broader feature is [work-in-progress](https://github.com/yugabyte/yugabyte-db/issues/1004), tablets can be automatically split for a few scenarios starting the v2.2 release.

### Enable automatic tablet splitting

To enable automatic tablet splitting, use the `yb-master` [`--tablet_split_size_threshold_bytes`](../../reference/configuration/yb-master/#tablet-split-size-threshold-bytes) flag to specify the size when tablets should split.

??? Need guidance and tips on what values to use.

**Example**

```sh

```

#### Use automatic tablet splitting with range-sharded tables

???

#### Use automatic tablet splitting with hash-sharded tables

???

### Example using a YCSB workload with automatic tablet splitting

In the following example, a three-node cluster is created and uses a YCSB workload to demonstrate the use of automatic tablet splitting in a YSQL database. For details on using YCSB with YugabyteDB, see the [YCSB](../../benchmark/ycsb-jdbc/) section in the Benchmark guide.

1. Create a three-node cluster.

```sh
./bin/yb-ctl --rf=3 create --num_shards_per_tserver=1 --ysql_num_shards_per_tserver=1 --master_flags '"tablet_split_size_threshold=30000000"' --tserver_flags '"memstore_size_mb=10"'
```

2. Create a table for workload.

```sh
./bin/ysqlsh -c "CREATE DATABASE ycsb;"
./bin/ysqlsh -d ycsb -c "CREATE TABLE usertable (
           YCSB_KEY TEXT,
           FIELD0 TEXT, FIELD1 TEXT, FIELD2 TEXT, FIELD3 TEXT,
           FIELD4 TEXT, FIELD5 TEXT, FIELD6 TEXT, FIELD7 TEXT,
           FIELD8 TEXT, FIELD9 TEXT,
           PRIMARY KEY (YCSB_KEY ASC));"
```

3. Create the properties file for YCSB at `~/code/YCSB/db-local.properties` and add the following content.

```sh
db.driver=org.postgresql.Driver
db.url=jdbc:postgresql://127.0.0.1:5433/ycsb;jdbc:postgresql://127.0.0.2:5433/ycsb;jdbc:postgresql://127.0.0.3:5433/ycsb
db.user=yugabyte
db.passwd=
core_workload_insertion_retry_limit=10
```

4. Start loading data for the workload.

```sh
~/code/YCSB/bin/ycsb load jdbc -s -P ~/code/YCSB/db-local.properties -P ~/code/YCSB/workloads/workloada -p recordcount=500000 -p operationcount=1000000 -p threadcount=4
```

5. Monitor the tablets splitting by going to the YugabyteDB Web UI at `http://localhost:7000/tablet-servers` and `http://127.0.0.1:9000/tablets`.

6. After the data is loaded, save the per-tablet metrics into a file:

The following assumes `curl` and `jq` are installed.

```sh
(curl -s http://127.0.0.1:9000/metrics && curl -s http://127.0.0.2:9000/metrics && curl -s http://127.0.0.3:9000/metrics) | jq -s '[flatten | .[] | select(.type == "tablet") as $tablet | .metrics[] | select(.name == "rocksdb_number_db_seek" and .value > 0) | { tablet_id: $tablet.id, rocksdb_number_db_seek: .value }] | sort_by (.tablet_id)' >after-load.json
```

7. Run the workload.

```sh
~/code/YCSB/bin/ycsb run jdbc -s -P ~/code/YCSB/db-local.properties -P ~/code/YCSB/workloads/workloada -p recordcount=500000 -p operationcount=1000000 -p threadcount=4
```

8. After the workload is completed, save the per-tablet metrics into another file.

```sh
(curl -s http://127.0.0.1:9000/metrics && curl -s http://127.0.0.2:9000/metrics && curl -s http://127.0.0.3:9000/metrics) | jq -s '[flatten | .[] | select(.type == "tablet") as $tablet | .metrics[] | select(.name == "rocksdb_number_db_seek" and .value > 0) | { tablet_id: $tablet.id, rocksdb_number_db_seek: .value }] | sort_by (.tablet_id)' >after-run.json
```

9. Get the list of tablets accessed during the run phase of the workload.

```sh
diff -C1 after-load.json after-run.json | grep tablet_id | sort | uniq
```

10. The list of tablets accessed can be compared with `http://127.0.0.1:9000/tablets` to make sure no pre-split tablets have been accessed.

