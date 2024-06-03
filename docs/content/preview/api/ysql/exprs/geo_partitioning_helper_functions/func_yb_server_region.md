---
title: yb_server_region() function [YSQL]
headerTitle: yb_server_region()
linkTitle: yb_server_region()
description: Returns the region of the currently connected node
menu:
  preview:
    identifier: api-ysql-exprs-yb_server_region
    parent: geo-partitioning-helper-functions
type: docs
---

## Synopsis

`yb_server_region()` returns the region of the currently connected node.

## Examples

Call `yb_server_region()`

```plpgsql
yugabyte=# SELECT yb_server_region();
```

```output.sql
 yb_server_region
-----------------
 us-west-1
(1 row)
```

## Usage in Row-level geo-partitioning

This function is primarily helpful while implementing [Row-level geo-partitioning](../../../../../explore/multi-region-deployments/row-level-geo-partitioning/), as it can significantly simplify **inserting**  rows from the user's connected node.

## Use case examples

### Setup

Do the following to create a 3-node multi-region cluster and a geo-partitioned table using tablespaces:

1. Create a cluster spread across 3 regions us-west-1, us-east-1, us-east-2 using yugabyted:

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

1. Use [yb-admin](../../../../../admin/yb-admin/) to specify the placement configuration to be used by the cluster:

    ```sh
    ./bin/yb-admin -master_addresses <IP1>:7100 modify_placement_info aws.us-west-1.us-west-1c:1,aws.us-east-1.us-east-1a:1,aws.us-east-2.us-east-2c:1 3
    ```

1. Create tablespaces corresponding to the regions used by the cluster created above [using ysqlsh](../../../../../admin/ysqlsh/#using-ysqlsh):

    ```sql
    CREATE TABLESPACE us_west_tablespace WITH (replica_placement=' {"num_replicas":1,"placement_blocks":[{"cloud":"aws","region":"us-west-1","zone":"us-west-1c","min_num_replicas":1}]}');
    CREATE TABLESPACE us_east1_tablespace WITH (replica_placement=' {"num_replicas":1,"placement_blocks":[{"cloud":"aws","region":"us-east-1","zone":"us-east-1a","min_num_replicas":1}]}');
    CREATE TABLESPACE us_east2_tablespace WITH (replica_placement=' {"num_replicas":1,"placement_blocks":[{"cloud":"aws","region":"us-east-2","zone":"us-east-2c","min_num_replicas":1}]}');
    ```

    For more information on how to set up a cluster with [yugabyted](../../../../../reference/configuration/yugabyted/) or [YugabyteDB Anywhere](https://www.yugabyte.com/anywhere/) with corresponding tablespaces, see [tablespaces](../../../../../explore/going-beyond-sql/tablespaces/).

1. Using the tablespaces, you can create a geo-partitioned table as follows. This is a partitioned table with 3 partitions, where each partition is pinned to a different location based on the regions. The geo_partition column value is default to be the currently connected region as in `yb_server_region()`.

    ```sql
    CREATE TABLE users(user_id INTEGER NOT NULL,
                       user_info VARCHAR NOT NULL,
                       geo_partition VARCHAR DEFAULT yb_server_region(),
                       PRIMARY KEY(user_id, geo_partition))
       PARTITION BY LIST(geo_partition);

    CREATE TABLE user_us_west PARTITION OF users FOR VALUES IN ('us-west-1') TABLESPACE us_west_tablespace;

    CREATE TABLE user_us_east1 PARTITION OF users FOR VALUES IN ('us-east-1') TABLESPACE us_east1_tablespace;

    CREATE TABLE user_us_east2 PARTITION OF users FOR VALUES IN ('us-east-2') TABLESPACE us_east2_tablespace;
    ```

1. Insert data to the `users` table:

    If your server is connected to region `us-west-1`, you can insert rows into the `users` table without having to specify the `geo_partition` column value.

    ```sql
    INSERT INTO users VALUES(1, 'US West user');
    ```

    If your server is connected to region `us-west-1` and you want to insert rows into another region's partitioned table, you can still insert the rows normally.

    ```sql
    INSERT INTO users VALUES(2, 'US East 1 user', 'us-east-1');
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

### Using yb_server_region() on a partitioned table

Assuming that the client is in the `us-west-1` region, using `yb_server_region()` in the `WHERE` clause causes YSQL to only scan the `us_user_west` table:

```sql
EXPLAIN (COSTS OFF) SELECT * FROM users WHERE geo_partition=yb_server_region();
```

```output
                           QUERY PLAN
-----------------------------------------------------------------
 Append
   Subplans Removed: 2
   ->  Seq Scan on user_us_west
         Filter: ((geo_partition)::text = (yb_server_region())::text)
(4 rows)
```

In other words, using `yb_server_region()` in the `WHERE` clause automatically returns only values from your current region.

```sql
SELECT * FROM users WHERE geo_partition=yb_server_region();
```

```output
 user_id |  user_info   | geo_partition
---------+--------------+---------------
       1 | US West user | us-west-1
(1 row)
```

{{< note title="Note" >}}

If you didn't set the placement_zone flag at node startup, yb_server_zone() returns NULL.

{{< /note >}}

## See also

- [`yb_server_cloud()`](../func_yb_server_cloud)
- [`yb_server_zone()`](../func_yb_server_zone)
- [`yb_is_local_table(oid)`](../func_yb_is_local_table)
