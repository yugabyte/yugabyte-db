---
title: Manage tables and indexes for disaster recovery
headerTitle: Manage tables and indexes
linkTitle: Tables and indexes
description: Manage tables and indexes in universes with disaster recovery
headContent: How to add and remove tables and indexes from a DR primary universe
menu:
  stable_yugabyte-platform:
    parent: disaster-recovery
    identifier: disaster-recovery-tables
    weight: 50
type: docs
---

When DDL changes are made to databases in replication for disaster recovery (DR) (such as creating, altering, or dropping tables or partitions), the changes have to be performed at the SQL level on both the DR primary and replica, and updated in the DR configuration. There is a specific order of these operations, which varies depending on whether performing a CREATE, DROP, ALTER, and so forth.

| Change to database on DR primary | Do the following |
| :----------- | :--- |
| CREATE TABLE | In DB on DR replica, use SQL to CREATE TABLE.<br>In YBA, add the table to replication. |
| DROP TABLE   | In DB on DR replica, use SQL to DROP TABLE |
| CREATE INDEX | In DB on DR replica, use SQL to CREATE INDEX.<br>In YBA, reconcile config with database. |
| DROP INDEX   | In DB on DR replica, use SQL to DROP INDEX.<br>In YBA, reconcile config with database. |
| CREATE TABLE foo PARTITION OF bar | Same as CREATE TABLE |

Use the following guidance when managing tables and indexes in universes with DR configured.

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
1. Select a storage config to be used for backup and restore in case a full copy needs to be transferred to the DR replica database.
1. Click **Apply Changes**.

Note the following:

- If the newly added table already has data, then adding the table can trigger a full copy of that entire database from DR primary to replica.

- You should always set up replication on the new table before starting any workload to ensure that data is protected at all times. This approach also avoids the full copy.

- This operation also automatically adds any associated index tables of this table to the DR configuration.

- If using colocation, colocated tables on the DR primary and replica should be created with the same colocation ID if they already exist on both the DR primary and replica prior to DR setup.

## Remove a table from DR

Remove tables from DR in the following sequence:

1. Navigate to your primary universe and select **Disaster Recovery**.
1. Click **Actions** and choose **Select Databases and Tables**.
1. Deselect the tables and click **Validate Selection**.
1. Drop the table from both DR primary and replica databases separately.

## Add an index to DR

Indexes are automatically added to replication in an atomic fashion after you create the indexes separately on DR primary and replica. You do not have to stop the writes on the primary.

**Note**: The Create Index DDL may kill some in-flight transactions. This is a temporary error. Retry any failed transactions.

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
