---
title: Handling DDLs in transactional xCluster
headerTitle: Handling DDLs in manual setup
linkTitle: Manual handling of DDLs
description: How to handle DDLs when using transactional xCluster replication between universes
headContent: Fully manual transactional xCluster
menu:
  preview:
    parent: async-replication-transactional
    identifier: async-transactional-tables
    weight: 50
badges: ysql
type: docs
---

The following instructions are for performing DDL operations (such as creating, altering, or dropping tables or partitions) on databases in [manually configured](../async-transactional-setup/) transactional xCluster replication. For instruction on making DDL changes in a semi-automatic setup, see [Making DDL changes](../async-transactional-setup-dblevel/#making-ddl-changes).

When making DDL changes, the statements must be executed on both the Primary/Source and Standby/Target and the xCluster configuration must be updated.

You should perform these actions in a specific order, depending on the type of DDL, as indicated in the following table.

| DDL | Step 1 | Step 2 |  Step 3 |
| :--- | :--- | :--- | :--- |
| CREATE TABLE | Execute DDL on Primary | Execute DDL on Standby | Add the table to replication |
| CREATE TABLE foo PARTITION OF bar | Execute DDL on Primary | Execute DDL on Standby | Add the table to replication |
| DROP TABLE   | Remove the table from replication | Execute DDL on Standby | Execute DDL on Primary |
| CREATE INDEX | Execute DDL on Primary | Execute DDL on Standby | - |
| DROP INDEX   | Execute DDL on Standby | Execute DDL on Primary | - |
| ALTER TABLE or INDEX | Execute DDL on Standby | Execute DDL on Primary | - |

## Tables

### Create table

To ensure that data is protected at all times, set up replication on a new table _before_ inserting any into it.

If a table already has data before adding it to replication, then adding the table to replication can result in a backup and restore of the entire database.

Add tables to replication in the following sequence:

1. Create the table on the Primary.
1. Create the table on the Standby.
1. Add the table to the replication.

    For instructions on adding tables to replication, refer to [Adding tables (or partitions)](../async-deployment/#adding-tables-or-partitions).

### Drop table

Remove tables from replication in the following sequence:

1. Remove the table from replication.

    For instructions on removing tables from replication, refer to [Removing objects](../async-deployment/#removing-objects).

1. Drop the table from both Primary and Standby databases separately.

## Indexes

### Create index

Indexes are automatically added to replication in an atomic fashion after you create the indexes separately on Primary and Standby. You do not have to stop the writes on the Primary.

**Note**: The Create Index DDL may kill some in-flight transactions. This is a temporary error. Retry any failed transactions.

Add indexes to replication in the following sequence:

1. Create an index on the Primary.

1. Wait for index backfill to finish.

1. Create the same index on Standby.

1. Wait for index backfill to finish.

    For instructions on monitoring backfill, refer to [Monitor index backfill from the command line](https://yugabytedb.tips/?p=2215).

### Drop index

When an index is dropped it is automatically removed from replication.

Remove indexes from replication in the following sequence:

1. Drop the index on the Standby universe.

1. Drop the index on the Primary universe.

## Table partitions

### Create table partition

Adding a table partition is similar to adding a table.

The caveat is that the parent table (if not already) along with each new partition has to be added to the replication, as DDL changes are not replicated automatically. Each partition is treated as a separate table and is added to replication separately (like a table).

For example, you can create a table with partitions as follows:

```sql
CREATE TABLE order_changes (
  order_id int,
  change_date date,
  type text,
  description text)
  PARTITION BY RANGE (change_date);  
```

```sql
CREATE TABLE order_changes_default PARTITION OF order_changes DEFAULT;
```

Create a new partition:

```sql
CREATE TABLE order_changes_2023_01 PARTITION OF order_changes
FOR VALUES FROM ('2023-01-01') TO ('2023-03-30');
```

Assume the parent table and default partition are included in the replication stream.

To add a table partition to the replication, follow the same steps for [Add a table to replication](#add-a-table-to-replication).

### Drop table partitions

To remove a table partition from replication, follow the same steps as [Remove a table from replication](#remove-a-table-from-replication).

## Alters

You can alter the schema of tables and indexes without having to stop writes on the Primary.

**Note**: The ALTER DDL may kill some in-flight transactions. This is a temporary error. Retry any failed transactions.

Alter the schema of tables and indexes in the following sequence:

1. Alter the index on the Standby universe.

1. Alter the index on the Primary universe.
