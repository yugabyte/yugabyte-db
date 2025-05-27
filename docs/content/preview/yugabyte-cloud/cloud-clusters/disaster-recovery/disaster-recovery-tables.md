---
title: Manage tables and indexes for disaster recovery in Aeon
headerTitle: Manage tables and indexes in DR
linkTitle: Tables and indexes
description: Manage tables and indexes in clusters with disaster recovery in Aeon
headContent: Add and remove tables and indexes in clusters with disaster recovery
menu:
  preview_yugabyte-cloud:
    parent: disaster-recovery-aeon
    identifier: disaster-recovery-tables-aeon
    weight: 50
type: docs
---

When making DDL changes to databases in replication for disaster recovery (DR) (such as creating, altering, or dropping tables or partitions), you must perform the changes at the SQL level on both the Source and Target.

For each DDL statement:

1. Execute the DDL on the Source, waiting for it to complete.
1. Execute the DDL on the Target, waiting for it to complete.

After both steps are complete, the YugabyteDB Aeon should reflect any added/removed tables in the Tables listing for this DR configuration.

In addition, keep in mind the following:

- If you are using Colocated tables, you CREATE TABLE on Source, then CREATE TABLE on Target making sure that you force the Colocation ID to be identical to that on Source.
- If you try to make a DDL change on Source and it fails, you must also make the same attempt on Target and get the same failure.
- TRUNCATE TABLE is not supported. To truncate a table, pause replication, truncate the table on both primary and standby, and resume replication.

Use the following guidance when managing tables and indexes in clusters with DR configured.

## Tables

Note: If you are performing application upgrades involving both adding and dropping tables, perform the upgrade in two parts: first add tables, then drop tables.

### Add a table to DR

To ensure that data is protected at all times, set up DR on a new table _before_ starting any workload.

If a table already has data before adding it to DR, then adding the table to replication can result in a backup and restore of the entire database from Source to Target.

Add tables to DR in the following sequence:

1. Create the table on the Source (if it doesn't already exist).
1. Create the table on the Target.
1. Navigate to your Source cluster **Disaster Recovery** tab and select the replication configuration.
1. Click **Actions** and choose **Select Databases and Tables**.
1. Select the tables and click **Validate Selection**.
1. If data needs to be copied, click **Next: Confirm Full Copy**.
1. Click **Apply Changes**.

Note the following:

- If the newly added table already has data, then adding the table can trigger a full copy of that entire database from Source to Target.

- It is recommended that you set up replication on the new table before starting any workload to ensure that data is protected at all times. This approach also avoids the full copy.

- This operation also automatically adds any associated index tables of this table to the DR configuration.

- If using colocation, colocated tables on the Source and Target should be created with the same colocation ID if they already exist on both the Source and Target prior to DR setup.

### Remove a table from DR

When dropping a table, remove the table from DR before dropping the table in the Source and Target databases.

Remove tables from DR in the following sequence:

1. Navigate to your Source cluster **Disaster Recovery** tab and select the replication configuration.
1. Click **Actions** and choose **Select Databases and Tables**.
1. Deselect the tables and click **Validate Selection**.
1. Click **Apply Changes**.
1. Drop the table from the Target database.
1. Drop the table from the Source database.

## Indexes

### Add an index to DR

Indexes are automatically added to replication in an atomic fashion after you create the indexes separately on Source and Target. You don't need to stop the writes on the Source.

CREATE INDEX may kill some in-flight transactions. This is a temporary error. Retry any failed transactions.

Add indexes to replication in the following sequence:

1. Create an index on the Source.

1. Wait for index backfill to finish.

1. Create the same index on the Target.

1. Wait for index backfill to finish.

    For instructions on monitoring backfill, refer to [Create indexes and track the progress](../../../../explore/ysql-language-features/indexes-constraints/index-backfill/).

1. [Reconcile the configuration](#reconcile-configuration).

### Remove an index from DR

When an index is dropped it is automatically removed from DR.

Remove indexes from replication in the following sequence:

1. Drop the index on the Target.

1. Drop the index on the Source.

1. [Reconcile the configuration](#reconcile-configuration).

## Table partitions

### Add a table partition to DR

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

### Remove table partitions from DR

To remove a table partition from DR, follow the same steps as [Remove a table from DR](#remove-a-table-from-dr).

## Reconcile configuration

To ensure changes made outside of YugabyteDB Aeon are reflected in YugabyteDB Aeon, you need to reconcile the configuration as follows:

1. Navigate to your Source cluster **Disaster Recovery** tab and select the replication configuration.
1. Click **Actions > Advanced** and choose **Reconcile Config with Database**.
