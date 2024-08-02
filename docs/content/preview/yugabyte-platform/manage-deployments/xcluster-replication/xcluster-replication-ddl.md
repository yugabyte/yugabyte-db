---
title: Manage tables and indexes for xCluster replication
headerTitle: Manage tables and indexes
linkTitle: Tables and indexes
description: Manage tables and indexes in universes in xCluster replication
headContent: Add and remove tables and indexes in universes with xCluster replication
menu:
  preview_yugabyte-platform:
    parent: xcluster-replication
    identifier: xcluster-replication-ddl
    weight: 30
type: docs
---

When DDL changes are made to databases in replication for xCluster replication (such as creating, altering, or dropping tables or partitions), the changes must be:

- performed at the SQL level on both the source and replica, and then
- updated at the YugabyteDB Anywhere level in the replication configuration.

## Order of operations

You should perform these operations in a specific order, depending on whether performing a CREATE, DROP, ALTER, and so forth, as indicated by the sequence number of the operation in the following table.

{{<tabpane text=true >}}
{{% tab header="YSQL Transactional" lang="ysql-transaction" %}}

| DDL | Step 1 | Step 2 | Step 3 |
| :-- | :----- | :----- | :----- |
| CREATE TABLE | Execute on Source | Execute on Target | [Add table to replication](#add-a-table-to-replication) |
| DROP TABLE<br>Preview | Execute on Target | Execute on Source | No changes needed |
| DROP TABLE<br>v2.20 or 2024.1 | [Remove table from replication](#remove-a-table-from-replication) | Execute on Target | Execute on Source |
| CREATE TABLE foo<br>PARTITION OF bar | Execute on Source | Execute on Target | [Add table to replication](#add-a-table-to-replication) |
| CREATE TABLE<br>Colocated | Execute on Target | Execute on Source using same colocation ID | No changes needed |
| CREATE INDEX | Execute on Source<br>wait for backfill | Execute&nbsp;on&nbsp;Target<br>wait for backfill | No changes needed<br>v2.20 or 2024.1 [Reconcile configuration](#reconcile-configuration) |
| DROP INDEX   | Execute on Target | Execute on Source | No changes needed<br>v2.20 or 2024.1 [Reconcile configuration](#reconcile-configuration) |
| ALTER TABLE or INDEX | Execute&nbsp;on&nbsp;Target | Execute on Source | No changes needed |
| ALTER TABLE<br>ADD CONSTRAINT UNIQUE | Execute on Source<br>wait for backfill | Execute on Target<br>wait for backfill | No changes needed<br>v2.20 or 2024.1 [Reconcile configuration](#reconcile-configuration) |
| ALTER TABLE<br>DROP&nbsp;CONSTRAINT<br>(unique constraints only) | Execute on Target | Execute on Source | No changes needed<br>v2.20 or 2024.1 [Reconcile configuration](#reconcile-configuration) |

{{% /tab %}}
{{% tab header="YSQL Non-Transactional" lang="ysql-nontransaction" %}}

| DDL | Step 1 | Step 2 | Step 3 |
| :-- | :----- | :----- | :----- |
| CREATE TABLE | Execute on Source | Execute on Target | [Add table to replication](#add-a-table-to-replication) |
| DROP TABLE<br>Preview | Execute on Target | Execute on Source | No changes needed |
| DROP TABLE<br>v2.20 or 2024.1 | [Remove table from replication](#remove-a-table-from-replication) | Execute on Target | Execute&nbsp;on&nbsp;Source |
| CREATE TABLE foo<br>PARTITION OF bar | Execute&nbsp;on&nbsp;Source | Execute on Target | [Add table to replication](#add-a-table-to-replication) |
| CREATE TABLE<br>Colocated | Execute on Target | Execute&nbsp;on&nbsp;Source using same colocation ID | No changes needed |
| CREATE INDEX | Execute on Source<br>wait for backfill | Execute&nbsp;on&nbsp;Target<br>wait for backfill | [Add table to replication](#add-a-table-to-replication) |
| DROP INDEX   | Execute on Target | Execute on Source | No changes needed |
| DROP INDEX<br>v2.20 or 2024.1 | [Remove table from replication](#remove-a-table-from-replication) | Execute on Target | Execute on Source |
| ALTER TABLE or INDEX | Execute&nbsp;on&nbsp;Target | Execute on Source | No changes needed |
| ALTER TABLE<br>ADD CONSTRAINT UNIQUE | Execute on Source<br>wait for backfill | Execute on Target<br>wait for backfill | Add the index table corresponding to the constraint from replication |
| ALTER TABLE<br>DROP&nbsp;CONSTRAINT<br>(unique constraints only)<br>Preview | Execute on Target | Execute on Source | No changes needed |
| ALTER TABLE<br>DROP&nbsp;CONSTRAINT<br>(unique constraints only)<br>v2.20 or 2024.1 | Remove the index table corresponding to the constraint from replication | Execute on Target | Execute on Source |

{{% /tab %}}
{{% tab header="YCQL" lang="ycql" %}}

| DDL | Step 1 | Step 2 | Step 3 |
| :-- | :----- | :----- | :----- |
| CREATE TABLE | Execute on Source | Execute on Target | [Add table to replication](#add-a-table-to-replication) |
| DROP TABLE   | [Remove table from replication](#remove-a-table-from-replication) | Execute on Target | Execute on Source |
| CREATE INDEX | Execute on Source | Execute&nbsp;on&nbsp;Target | [Reconcile&nbsp;configuration](#reconcile-configuration) |
| DROP INDEX   | [Remove index table from replication](#remove-a-table-from-replication) | Execute on Target | Execute on Source |
| ALTER TABLE or INDEX | Execute&nbsp;on&nbsp;Target | Execute&nbsp;on&nbsp;Source | No changes needed |

{{% /tab %}}
{{</tabpane >}}

In addition, keep in mind the following:

- If you are using Colocated tables, you CREATE TABLE on source, then CREATE TABLE on target making sure that you force the Colocation ID to be identical to that on source.
- If you try to make a DDL change on source and it fails, you must also make the same attempt on target and get the same failure.

Use the following guidance when managing tables and indexes in universes with replication configured.

## Tables

Note: If you are performing application upgrades involving both adding and dropping tables, perform the upgrade in two parts: first add tables, then drop tables.

### Add a table to replication

To ensure that data is protected at all times, set up replication on a new table _before_ starting any workload.

If a table already has data before adding it to replication, then adding the table to replication can result in a backup and restore of the entire database from source to target.

Add tables to replication in the following sequence:

1. Create the table on the source (if it doesn't already exist).
1. Create the table on the target.
1. Navigate to your source and select **xCluster Replication**.
1. Click **Actions** and choose **Select Databases and Tables**.
1. Select the tables and click **Validate Selection**.
1. If data needs to be copied, click **Next: Confirm Full Copy**.
1. Click **Apply Changes**.

Note the following:

- If the newly added table already has data, then adding the table can trigger a full copy of that entire database from source to replica.

- It is recommended that you set up replication on the new table before starting any workload to ensure that data is protected at all times. This approach also avoids the full copy.

- This operation also automatically adds any associated index tables of this table to the replication configuration.

- If using colocation, colocated tables on the source and replica should be created with the same colocation ID if they already exist on both the source and replica prior to replication setup.

### Remove a table from replication

When dropping a table, remove the table from replication before dropping the table in the source and replica databases.

Remove tables from replication in the following sequence:

1. Navigate to your source and select **xCluster Replication**.
1. Click **Actions** and choose **Select Databases and Tables**.
1. Deselect the tables and click **Validate Selection**.
1. Click **Apply Changes**.
1. Drop the table from the target database.
1. Drop the table from the source database.

## Indexes

### Add an index to replication

Indexes are automatically added to replication in an atomic fashion after you create the indexes separately on source and replica. You don't need to stop the writes on the source.

CREATE INDEX may kill some in-flight transactions. This is a temporary error. Retry any failed transactions.

Add indexes to replication in the following sequence:

1. Create an index on the source.

1. Wait for index backfill to finish.

1. Create the same index on the target.

1. Wait for index backfill to finish.

    For instructions on monitoring backfill, refer to [Create indexes and track the progress](../../../../explore/ysql-language-features/indexes-constraints/index-backfill/).

1. [Reconcile the configuration](#reconcile-configuration).

### Remove an index from replication

When an index is dropped it is automatically removed from replication.

Remove indexes from replication in the following sequence:

1. Drop the index on the target.

1. Drop the index on the source.

1. [Reconcile the configuration](#reconcile-configuration).

## Table partitions

### Add a table partition to replication

Adding a table partition is similar to adding a table.

The caveat is that the parent table (if not already) along with each new partition has to be added to replication, as DDL changes are not replicated automatically. Each partition is treated as a separate table and is added to replication separately (like a table).

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

To add a table partition to replication, follow the same steps for [Add a table to replication](#add-a-table-to-replication).

### Remove table partitions from replication

To remove a table partition from replication, follow the same steps as [Remove a table from replication](#remove-a-table-from-replication).

## Reconcile configuration

To ensure changes made outside of YugabyteDB Anywhere are reflected in YugabyteDB Anywhere, you need to reconcile the configuration as follows:

1. In YugabyteDB Anywhere, navigate to your source and select **xCluster Replication**.
1. Click **Actions > Advanced** and choose **Reconcile Config with Database**.
