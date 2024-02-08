---
title: yb_is_local_table() function [YSQL]
headerTitle: yb_is_local_table(oid)
linkTitle: yb_is_local_table()
description: Returns whether the given 'oid' is a table replicated only in the local region.
menu:
  v2.18:
    identifier: api-ysql-exprs-yb_is_local_table
    parent: geo-partitioning-helper-functions
type: docs
---

## Synopsis

`yb_is_local_table` indicates whether a table is pinned to the local region. The function takes a table's OID (object identifier), and looks for a tablespace associated with that table. If the tablespace is confined to the same region as that of the YugabyteDB node processing the query, the function returns `true`. Otherwise, the function returns `false`.

{{< note title="Note" >}}

* Passing the OID of a _temporary_ table will always return _true_ as temporary tables are considered local.
* Tables not assigned a tablespace will always be considered remote.

{{< /note >}}

## Usage in Row-level geo-partitioning

This function is primarily helpful while implementing [Row-level geo-partitioning](../../../../../explore/multi-region-deployments/row-level-geo-partitioning/), as it can significantly simplify selecting rows from the local partition. Every table contains a system column called `tableoid`. This stores the `oid` of the table to which the row belongs. While querying a partitioned table, the `tableoid` column thus returns the `oid` of the partition to which the row belongs. The following sections describe how the `tableoid` column can be used with `yb_is_local_table` function to query the local partition.

## Use case examples

### Setup

 You can create a 3 node multi-region cluster and a geo-partitioned table using tablespaces with the following steps:

1. Create a cluster spread across 3 regions us-west-1, us-east-1, us-east-2 using yugabyted as follows:

    ```sh
    ./bin/yugabyted start                           \
      --base_dir=/home/yugabyte/<IP1>/yugabyte-data \
      --listen=<IP1>                                \
      --master_flags "placement_cloud=aws,placement_region=us-west-1,placement_zone=us-west-1c" \
      --tserver_flags "placement_cloud=aws,placement_region=us-west-1,placement_zone=us-west-1c"

    ./bin/yugabyted start                           \
      --base_dir=/home/yugabyte/<IP2>/yugabyte-data \
      --listen=<IP2>                                \
      --join=<IP1>                                  \
      --master_flags "placement_cloud=aws,placement_region=us-east-2,placement_zone=us-east-2c" \
      --tserver_flags "placement_cloud=aws,placement_region=us-east-2,placement_zone=us-east-2c"

    ./bin/yugabyted start                            \
      --base_dir=/home/yugabyte/<IP3>/yugabyte-data  \
      --listen=<IP3>                                 \
      --join=<IP1>                                   \
      --master_flags "placement_cloud=aws,placement_region=us-east-1,placement_zone=us-east-1a" \
      --tserver_flags "placement_cloud=aws,placement_region=us-east-1,placement_zone=us-east-1a"
    ```

1. Use [yb-admin](../../../../../admin/yb-admin/) to specify the placement configuration to be used by the cluster as follows:

    ```sh
    ./bin/yb-admin -master_addresses <IP1>:7100 modify_placement_info aws.us-west-1.us-west-1c:1,aws.us-east-1.us-east-1a:1,aws.us-east-2.us-east-2c:1 3
    ```

1. Create tablespaces corresponding to the regions used by the cluster created above [using ysqlsh](../../../../../admin/ysqlsh/#using-ysqlsh) as follows:

    ```sql
    CREATE TABLESPACE us_west_tablespace WITH (replica_placement=' {"num_replicas":1,"placement_blocks":[     {"cloud":"aws","region":"us-west-1","zone":"us-west-1c","min_num_replicas":1}]}');
    CREATE TABLESPACE us_east1_tablespace WITH (replica_placement=' {"num_replicas":1,"placement_blocks":[     {"cloud":"aws","region":"us-east-1","zone":"us-east-1a","min_num_replicas":1}]}');
    CREATE TABLESPACE us_east2_tablespace WITH (replica_placement=' {"num_replicas":1,"placement_blocks":[     {"cloud":"aws","region":"us-east-2","zone":"us-east-2c","min_num_replicas":1}]}');
    ```

     For more information on how to set up a cluster with [yugabyted](../../../../../reference/configuration/yugabyted/) or [YugabyteDB Anywhere](https://www.yugabyte.com/anywhere/) with corresponding tablespaces, see [tablespaces](../../../../../explore/ysql-language-features/going-beyond-sql/tablespaces).

1. Using the tablespaces, you can create a geo-partitioned table as follows. This is a partitioned table with 3 partitions, where each partition is pinned to a different location.

    ```sql
    CREATE TABLE users(user_id INTEGER NOT NULL,
                       user_info VARCHAR NOT NULL,
                       geo_partition VARCHAR NOT NULL,
                       PRIMARY KEY(user_id, geo_partition))
       PARTITION BY LIST(geo_partition);

    CREATE TABLE user_us_west PARTITION OF users FOR VALUES IN ('us-west') TABLESPACE us_west_tablespace;

    CREATE TABLE user_us_east1 PARTITION OF users FOR VALUES IN ('us-east1') TABLESPACE     us_east1_tablespace;

    CREATE TABLE user_us_east2 PARTITION OF users FOR VALUES IN ('us-east2') TABLESPACE     us_east2_tablespace;
    ```

1. Insert some sample data to the `users` table as follows:

    ```sql
    INSERT INTO users VALUES(1, 'US east user', 'us-east1');
    INSERT INTO users VALUES(2, 'US west user', 'us-west');
    INSERT INTO users VALUES(3, 'US central user', 'us-east2');
    ```

1. In a partitioned setup, if there are no `WHERE` clause restrictions on the partition key, note that every query on a partitioned table gets fanned out to all of its child partitions:

    ```sql
    EXPLAIN (COSTS OFF) SELECT * FROM users;
    ```

    ```output
                                    QUERY PLAN
    ---------------------------------------------------------------------------
     Append
       ->  Seq Scan on user_us_east2
       ->  Seq Scan on user_us_east1
       ->  Seq Scan on user_us_west
    (4 rows)
    ```

### Using yb_is_local_table() on a partitioned table

Assuming that the client is in the `us-west` region, note that using `yb_is_local_table()` in the `WHERE` clause causes YSQL to only scan the `us_user_west` table:

```sql
EXPLAIN (COSTS OFF) SELECT * FROM users WHERE yb_is_local_table(tableoid);
```

```output
                 QUERY PLAN
---------------------------------------------
 Append
   ->  Seq Scan on user_us_west
         Filter: yb_is_local_table(tableoid)
(3 rows)
```

```sql
SELECT * FROM users WHERE yb_is_local_table(tableoid);
```

```output
SELECT * FROM users WHERE yb_is_local_table(tableoid);
 user_id |  user_info   | geo_partition
---------+--------------+---------------
       2 | US west user | us-west
(1 row)
```

### JOINs

The `yb_is_local_table` function can also be used while performing JOINs, to filter out results.
Assume another partitioned table `users_transactions` and some sample data as follows:

```sql
CREATE TABLE users_transactions (user_id INTEGER NOT NULL,
                                transaction_id INTEGER NOT NULL,
                                geo_partition VARCHAR NOT NULL)
   PARTITION BY LIST(geo_partition);

-- Create 3 partitions for each tablespace.
CREATE TABLE user_txn_west PARTITION OF users_transactions FOR VALUES IN ('us-west') TABLESPACE us_west_tablespace;
CREATE TABLE user_txn_east1 PARTITION OF users_transactions FOR VALUES IN ('us-east1') TABLESPACE us_east1_tablespace;
CREATE TABLE user_txn_east2 PARTITION OF users_transactions FOR VALUES IN ('us-east2') TABLESPACE us_east2_tablespace;

-- Insert a row into each partition.
INSERT INTO users_transactions VALUES(1, 3789, 'us-east1');
INSERT INTO users_transactions VALUES(2, 5276, 'us-west');
INSERT INTO users_transactions VALUES(3, 2984, 'us-east2');
```

To perform a `JOIN` across the local partitions of both the tables, you can run the following query with two `WHERE` clauses, one for each partitioned table.

```sql
SELECT * FROM users, users_transactions WHERE users.user_id = users_transactions.user_id AND yb_is_local_table(users.tableoid) AND yb_is_local_table(users_transactions.tableoid);
```

```output
 user_id |  user_info   | geo_partition | user_id | transaction_id | geo_partition
---------+--------------+---------------+---------+----------------+---------------
       2 | US west user | us-west       |       2 |           5276 | us-west
(1 row)
```

Note that only the local partition in both the partitioned tables is being scanned.

```sql
EXPLAIN (COSTS OFF) SELECT * FROM users, users_transactions WHERE users.user_id = users_transactions.user_id AND yb_is_local_table(users.tableoid) AND yb_is_local_table(users_transactions.tableoid);
```

```output
                          QUERY PLAN
--------------------------------------------------------------
 Merge Join
   Merge Cond: (user_us_west.user_id = user_txn_west.user_id)
   ->  Sort
         Sort Key: user_us_west.user_id
         ->  Append
               ->  Seq Scan on user_us_west
                     Filter: yb_is_local_table(tableoid)
   ->  Sort
         Sort Key: user_txn_west.user_id
         ->  Append
               ->  Seq Scan on user_txn_west
                     Filter: yb_is_local_table(tableoid)
(12 rows)

```

### Other applications

The `yb_is_local_table` function can be used on any database object that can be associated with a tablespace. For instance, the following query can be used to list all the indexes and tables that have been tied to a local tablespace:

```sql
SELECT oid, relname from pg_class WHERE yb_is_local_table(oid);
```

```output
  oid  |      relname
-------+-------------------
 16392 | user_us_west
 16395 | user_us_west_pkey
 16410 | user_txn_west
(3 rows)
```

## Limitations

* Usage of this function is not optimized for `UPDATE` and `DELETE` queries. The planner will still scan all the partitions to find the rows that are present in the local partition.

* Usage of this function is not optimized when it is part of a bigger expression. For example, `WHERE !yb_is_local_table(tableoid)`. In this case also, the planner will still scan all the partitions to find the rows that are not present in the local partition.
