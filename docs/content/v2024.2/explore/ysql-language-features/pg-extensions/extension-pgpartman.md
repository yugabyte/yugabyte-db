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
  - partman - Create a trigger-based partition set (deprecated)

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

`part_config` table is used to make decisions about:

- What new partitions to create?
- Which old partitions to drop or detach from the partition set?
- Updates to the table re-configure the partition maintenance

An example is as follows:

```sql
SELECT parent_table, control, partition_interval, premake, retention FROM partman.part_config;
```

```output
 parent_table  |  control   | partition_interval | premake | retention
---------------+------------+--------------------+---------+-----------
 public.orders | order_date | 1 mon              |       5 |
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
