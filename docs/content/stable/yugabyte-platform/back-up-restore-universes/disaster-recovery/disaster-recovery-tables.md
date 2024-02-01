---
title: Manage tables and indexes for disaster recovery
headerTitle: Manage tables and indexes
linkTitle: Tables and indexes
description: Manage tables and indexes in universes with disaster recovery
headContent: Add and remove tables and indexes in universes with disaster recovery
menu:
  stable_yugabyte-platform:
    parent: disaster-recovery
    identifier: disaster-recovery-tables
    weight: 50
type: docs
---

When DDL changes are made to databases in replication for disaster recovery (DR) (such as creating, altering, or dropping tables or partitions), the changes must be:

- performed at the SQL level on both the DR primary and replica, and then
- updated at the YBA level in the DR configuration.

You should perform these actions in a specific order, depending on whether performing a CREATE, DROP, ALTER, and so forth.

| DB Change&nbsp;on&nbsp;DR&nbsp;primary | On DR replica | In YBA |
| :----------- | :----------- | :--- |
| CREATE TABLE | CREATE TABLE | Add the table to replication |
| DROP TABLE   | DROP TABLE   | Remove the table from replication before executing the DROP TABLE on the database. |
| CREATE INDEX | CREATE INDEX | [Resynchronize](#resynchronize-yba) |
| DROP INDEX   | DROP INDEX   | [Resynchronize](#resynchronize-yba) |
| CREATE TABLE foo PARTITION OF bar | Same as CREATE TABLE | |

Use the following guidance when managing tables and indexes in universes with DR configured.

## Best practices

If you are performing application upgrades involving both adding and dropping tables, perform the upgrade in two parts: first add tables, then drop tables.

## Add a table to DR

To ensure that data is protected at all times, set up DR on a new table _before_ starting any workload.

If a table already has data before adding it to DR, then adding the table to replication can result in a backup and restore of the entire database from DR primary to replica.

Add tables to DR in the following sequence:

1. Create the table on the DR primary (if it doesn't already exist).
1. Create the table on the DR replica.
1. Navigate to your primary universe and select **Disaster Recovery**.
1. Click **Actions** and choose **Select Databases and Tables**.
1. Select the databases to be copied to the DR replica for disaster recovery.

    You can add databases containing colocated tables to the DR configuration as long as the underlying database is v2.18.1.0 or later. Colocated tables on the DR primary and replica should be created with the same colocation ID if they already exist on both the DR primary and replica prior to DR setup.

1. Click **Validate Selection**.
1. If the validation is successful, click **Next: Configure Full Copy**.
1. Select a storage configuration to be used for backup and restore in case a full copy needs to be transferred to the DR replica database.
1. Click **Apply Changes**.

Note the following:

- If the newly added table already has data, then adding the table can trigger a full copy of that entire database from DR primary to replica.

- It is recommended that you set up replication on the new table before starting any workload to ensure that data is protected at all times. This approach also avoids the full copy.

- This operation also automatically adds any associated index tables of this table to the DR configuration.

- If using colocation, colocated tables on the DR primary and replica should be created with the same colocation ID if they already exist on both the DR primary and replica prior to DR setup.

## Remove a table from DR

When dropping a table, remove the table from DR before dropping the table in the DR primary and replica databases.

Remove tables from DR in the following sequence:

1. Navigate to your primary universe and select **Disaster Recovery**.
1. Click **Actions** and choose **Select Databases and Tables**.
1. Deselect the tables and click **Validate Selection**.
1. Drop the table from both DR primary and replica databases separately.

## Add an index to DR

Indexes are automatically added to replication in an atomic fashion after you create the indexes separately on DR primary and replica. You don't need to stop the writes on the primary.

CREATE INDEX may kill some in-flight transactions. This is a temporary error. Retry any failed transactions.

Add indexes to replication in the following sequence:

1. Create an index on the DR primary.

1. Wait for index backfill to finish.

1. Create the same index on DR replica.

1. Wait for index backfill to finish.

    For instructions on monitoring backfill, refer to [Monitor index backfill from the command line](https://yugabytedb.tips/?p=2215).

1. [Resynchronize YBA](#resynchronize-yba).

## Remove an index from DR

When an index is dropped it is automatically removed from DR.

Remove indexes from replication in the following sequence:

1. Drop the index on the DR replica.

1. Drop the index on the primary.

1. [Resynchronize YBA](#resynchronize-yba).

## Add a table partition to DR

Adding a table partition is similar to adding a table.

The caveat is that the parent table (if not already) along with each new partition has to be added to DR, as DDL changes are not replicated automatically. Each partition is treated as a separate table and is added to DR separately (like a table).

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

To add a table partition to DR, follow the same steps for [Add a table to DR](#add-a-table-to-dr).

## Remove table partitions from DR

To remove a table partition from DR, follow the same steps as [Remove a table from DR](#remove-a-table-from-dr).

## Resynchronize YBA

To ensure changes made outside of YugabyteDB Anywhere are reflected in YBA, resynchronize the YBA UI as follows:

1. Navigate to your primary universe and select **Disaster Recovery**.
1. Click **Actions > Advanced** and choose **Reconcile Config with Database**.
