---
title: pg_partman extension
headerTitle: Partition manager extension
linkTitle: pg_partman
description: Using the pg_partman extension in YugabyteDB
tags:
  feature: early-access
menu:
  v2024.2:
    identifier: extension-pgpartman
    parent: pg-extensions
    weight: 20
type: docs
---

pg_partman is an extension to create and manage both time-based and serial-based table partition sets.

Partitioning in YugabyteDB refers to physically dividing large tables into smaller, more manageable tables to improve performance. Typically, tables with columns containing timestamps are subject to partitioning because of the historical and predictable nature of their data.

{{<lead link="../../../ysql-language-features/advanced-features/partitions/">}}
For more information, see [Table partitioning](../../../ysql-language-features/advanced-features/partitions/).
{{</lead>}}

PostgreSQL Partition Manager (pg_partman) is an extension that simplifies managing table partitions based on time or serial IDs, automating their creation and maintenance. While it includes many options, only a few are typically needed, making it user-friendly.

{{<lead link="https://github.com/yugabyte/yugabyte-db/blob/master/src/postgres/third-party-extensions/pg_partman/doc/pg_partman.md">}}
For a full list of options, see the [pg_partman documentation](https://github.com/yugabyte/yugabyte-db/blob/master/src/postgres/third-party-extensions/pg_partman/doc/pg_partman.md).
{{</lead>}}

YugabyteDB fully supports pg_partman features. Although it has some minor [limitations](#limitations) because it's a distributed database, these don't hinder partition management.

## Enable pg_partman

Enable the pg_partman extension separately for each database.

To enable the pg_partman extension for a specific database, create the partition maintenance schema (by default "public" schema is selected), and then create the pg_partman extension as follows:

```sql
CREATE SCHEMA partman;
CREATE EXTENSION pg_partman WITH SCHEMA partman;
```

## Use pg_partman

To demonstrate how to use the pg_partman extension, the following sample database table, "orders" is used which is partitioned based on a timestamp column:

```sql
CREATE TABLE orders (
    order_id SERIAL,
    order_date DATE NOT NULL,
    customer_id INT
) PARTITION BY RANGE (order_date);
```

### create_parent function

After you enable the pg_partman extension, you can use the `create_parent` function to set up table partitions. This function allows you to configure partitions within the schema designated for partition maintenance.

Use the `create_parent` function as follows:

```sql
SELECT partman.create_parent(
    p_parent_table => 'public.orders',
    p_control => 'order_date',
    p_type => 'native',
    p_interval => 'monthly',
    p_premake => 5
);
```

- p_parent_table - Parent partitioned table.
- p_control - Column on which table partitioning is done. Must be an integer or time-based.
- p_type - Either 'native' or 'partman'.

  - native - Use the native partitioning methods that are built into PostgreSQL 10+.
  - partman - Create a trigger-based partition set (disabled in YugabyteDB)

- p_interval – The time interval or integer range for each partition. For example, daily, hourly, monthly, and so on.
- p_premake – The number of partitions to create in advance to support new inserts.

{{<lead link="https://github.com/yugabyte/yugabyte-db/blob/master/src/postgres/third-party-extensions/pg_partman/doc/pg_partman.md#creation-functions">}}
For a complete description of the `create_parent` function, see [Creation Functions](https://github.com/yugabyte/yugabyte-db/blob/master/src/postgres/third-party-extensions/pg_partman/doc/pg_partman.md#creation-functions) in the pg_partman documentation.
{{</lead>}}

### part_config table

`part_config` table stores all configuration data for partition sets managed by pg_partman. New row for the partition table is added after calling [create_parent function](#create_parent-function).

{{<lead link="https://github.com/yugabyte/yugabyte-db/blob/master/src/postgres/third-party-extensions/pg_partman/doc/pg_partman.md#tables">}}
For more information, see [tables](https://github.com/yugabyte/yugabyte-db/blob/master/src/postgres/third-party-extensions/pg_partman/doc/pg_partman.md#tables) in pg_partman documentation.
{{</lead>}}

`part_config` table can be used to make decisions about:

- What new partitions to create?
- Which old partitions to drop or detach from the partition set?
- What table updates are required?

An example is as follows:

```sql
SELECT parent_table, control, partition_interval, premake, retention FROM partman.part_config;
```

```output
 parent_table  |  control   | partition_interval | premake | retention
---------------+------------+--------------------+---------+-----------
 public.orders | order_date | 1 mon              |       5 |
```

You can update the part_config table as follows:

```sql
UPDATE partman.part_config
SET partition_interval = 2 months
WHERE parent_table = 'public.orders';
```

### Partition maintenance with pg_cron

[pg_cron](../extension-pgcron) is a cron-based job scheduler for PostgreSQL that runs inside a database as an extension.

You can schedule the pg_partman function `run_maintenance` which automatically creates child tables and drops old child partitions for partition sets configured to use with the following steps:

1. Load the pg_cron extension as follows:

    ```sql
    CREATE EXTENSION pg_cron;
    ```

1. Update the part_config table to the retention related fields as follows:

    ```sql
    UPDATE partman.part_config
    SET retention = '3 months',
    retention_keep_table=true
    WHERE parent_table = 'public.orders';
    ```

    - `retention = '3 months',` - Configures the table to have a maximum retention of three months.
    - `retention_keep_table=true` - Configures that the table is only detached from the partition set and isn't deleted automatically.

1. Schedule the `run_maintenance` call via `cron.schedule` procedure as follows:

    ```sql
    SELECT cron.schedule(
        'Run maintenance job',
        '10 seconds',
        'SELECT partman.run_maintenance()'
    );
    ```

1. Monitor the scheduled maintenance job by querying the `cron.job` table as follows:

    ```sql
    SELECT jobid, schedule, command, jobname FROM cron.job;
    ```

    ```output

     jobid |  schedule  |             command              |       jobname
    -------+------------+----------------------------------+----------+-------------
         1 | 10 seconds | SELECT partman.run_maintenance() | Run maintenance job
    ```

## Limitations

To use the `pg_partman` extension in YugabyteDB, following are some important limitations and modifications to be aware of:

- Partitioning methods supported

  Only native partitioning is supported in YugabyteDB. Non-native partitioning, which utilizes table inheritance and triggers to route data to the appropriate child tables, is not supported because YugabyteDB does not support table inheritance.

  Versions of `pg_partman` later than 4.7.4 have deprecated non-native partitioning, so migrating partition sets from non-native to native partitioning methods can be challenging.

  An example of an unsupported operation that would attempt to use non-native partitioning is as follows:

  ```sql
  new_db=# CREATE SCHEMA partman;
  n WITH SCHEMA partman;

  CREATE TABLE orders (
  order_id SERIAL,
  order_date DATE NOT NULL,
  customer_id INT
  ) PARTITION BY RANGE (order_date);

  new_db=# CREATE EXTENSION pg_partman WITH SCHEMA partman;

  new_db=# CREATE TABLE orders (
  new_db(# order_id SERIAL,
  new_db(# order_date DATE NOT NULL,
  new_db(# customer_id INT
  new_db(# ) PARTITION BY RANGE (order_date);

  new_db=# SELECT partman.create_parent( p_parent_table => 'public.orders',
  new_db(#  p_control => 'order_date',
  new_db(#  p_type => 'partman',
  new_db(#  p_interval =>'monthly',
  new_db(#  p_premake => 5);
  ```

  ```output
  ERROR:  partman is not a valid partitioning type for pg_partman
  ```

- ACCESS EXCLUSIVE LOCK

  The `create_parent` function of pg_partman requires an "access exclusive lock" on the parent table to create new child partitions. Currently access exclusive locks are not supported in YugabyteDB and so is disabled in this function.

- ADVISORY LOCKS

  Advisory locks, used in some `pg_partman` functions to create, drop/delete partitioned tables, are not supported in YugabyteDB. Attempts to acquire these locks will be disabled.

- Background worker process

  The pg_partman background worker process, which automates partition maintenance without the need of an external scheduler, is disabled in favor of using the `pg_cron` extension to manage such tasks externally.

- `undo_partition` function

  The `undo_partition` function, which moves data from child tables back to a target table is disabled.

  As part of `undo_partition`, all data from child partitions is moved out to the target table and after that the child table is detached from the parent table. This is done in an iterative manner looping through each child.

  The `undo_partition` function is disabled because YugabyteDB does not currently support transactional DDL operations (the DMLs (deleting data from child tables) and DDLs (detaching child tables) will cause conflicts), which are required for this functionality.

  For example,

  ```sql
  new_db=# select partman.undo_partition(p_parent_table => 'public.orders');
  ```

  ```output
  ERROR:  undo_partition not supported yet in YugabyteDB
  ```

- Default partition creation

  Creation and use of default partitions are disabled. If a default partition is created, `partition_data_proc` and `run_maintenance` operations will exit early without performing any operation.

  For example,

  ```sql
  yugabyte=# SELECT partman.create_parent( p_parent_table => 'public.orders',
  yugabyte(#  p_control => 'order_date',
  yugabyte(#  p_type => 'native',
  yugabyte(#  p_interval =>'monthly',
  yugabyte(#  p_premake => 5);
  ```

  ```output
   create_parent
  ---------------
   t
  (1 row)
  ```

  ```sql
  yugabyte=# \d
  ```

  ```output
                       List of relations
   Schema |        Name         |       Type        |  Owner
  --------+---------------------+-------------------+----------
   public | orders              | partitioned table | yugabyte
   public | orders_order_id_seq | sequence          | yugabyte
   public | orders_p2024_06     | table             | yugabyte
   public | orders_p2024_07     | table             | yugabyte
   public | orders_p2024_08     | table             | yugabyte
   public | orders_p2024_09     | table             | yugabyte
   public | orders_p2024_10     | table             | yugabyte
   public | orders_p2024_11     | table             | yugabyte
   public | orders_p2024_12     | table             | yugabyte
   public | orders_p2025_01     | table             | yugabyte
   public | orders_p2025_02     | table             | yugabyte
   public | orders_p2025_03     | table             | yugabyte
   public | orders_p2025_04     | table             | yugabyte
  (13 rows)

  No default partition created
  ```

  If you try to create default partition explicitly and call `run_maintenance`, the run maintenance job is skipped as follows:

  ```sql
  yugabyte=# CREATE TABLE orders_default PARTITION OF orders DEFAULT;
  yugabyte=# SELECT partman.run_maintenance('public.orders');
  ```

  ```output
  NOTICE:  Skip run maintenace job for table public.orders as it has default table attached
   run_maintenance
  -----------------

  (1 row)
  ```

- Colocated databases

  Registration of a partitioned table for maintenance by `pg_partman` is not allowed if the table belongs to a colocated database.

  For example,

  ```sql
  yugabyte=# CREATE DATABASE mo WITH colocation=true;
  yugabyte=# \c mo

  mo=# SELECT create_parent( p_parent_table => 'public.orders',
   p_control => 'order_date',
   p_type => 'native',
   p_interval =>'monthly',
   p_premake => 5);

  mo=# CREATE extension pg_partman;

  mo=# SELECT create_parent( p_parent_table => 'public.orders',
   p_control => 'order_date',
   p_type => 'native',
   p_interval =>'monthly',
   p_premake => 5);
  ```

  ```output
  ERROR:  Partition table public.orders is a colocated table hence registering it to pg_partman maintenance is not supported
  ```

This set of limitations ensures that the features of `pg_partman` align with the capabilities and architecture of YugabyteDB, maintaining system performance and stability. You should adjust your use of partitioning strategies accordingly to adhere to these constraints.
