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

When making DDL changes to databases in xCluster replication (such as creating, altering, or dropping tables or partitions), you must do the following:

- Make the change at the SQL level on both the source and target.
- Update the xCluster replication configuration in YugabyteDB Anywhere.

The order in which you do this varies depending on the operation.

## Order of operations

Perform DDL operations in the order as shown in the following table.

{{<tabpane text=true >}}
{{% tab header="YSQL Transactional" lang="ysql-transaction" %}}

| DDL | Step 1 | Step 2 | Step 3 |
| :-- | :----- | :----- | :----- |
| CREATE TABLE | Execute on Source | Execute on Target | [Add table to replication](#add-a-table-to-replication) |
| DROP TABLE<br>v2.23 or later | Execute on Target | Execute on Source | No changes needed |
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
| DROP TABLE<br>v2.23 or later | Execute on Target | Execute on Source | No changes needed |
| DROP TABLE<br>v2.20 or 2024.1 | [Remove table from replication](#remove-a-table-from-replication) | Execute on Target | Execute&nbsp;on&nbsp;Source |
| CREATE TABLE foo<br>PARTITION OF bar | Execute&nbsp;on&nbsp;Source | Execute on Target | [Add table to replication](#add-a-table-to-replication) |
| CREATE TABLE<br>Colocated | Execute on Target | Execute&nbsp;on&nbsp;Source using same colocation ID | No changes needed |
| CREATE INDEX | Execute on Source<br>wait for backfill | Execute&nbsp;on&nbsp;Target<br>wait for backfill | [Add index table to replication](#add-a-table-to-replication) |
| DROP INDEX   | Execute on Target | Execute on Source | No changes needed |
| DROP INDEX<br>v2.20 or 2024.1 | [Remove index table from replication](#remove-a-table-from-replication) | Execute on Target | Execute on Source |
| ALTER TABLE or INDEX | Execute&nbsp;on&nbsp;Target | Execute on Source | No changes needed |
| ALTER TABLE<br>ADD CONSTRAINT UNIQUE | Execute on Source<br>wait for backfill | Execute on Target<br>wait for backfill | [Add the index table](#add-a-table-to-replication) corresponding to the constraint to replication |
| ALTER TABLE<br>DROP&nbsp;CONSTRAINT<br>(unique&nbsp;constraints&nbsp;only)<br>v2.23 or later | Execute on Target | Execute on Source | No changes needed |
| ALTER TABLE<br>DROP&nbsp;CONSTRAINT<br>(unique constraints only)<br>v2.20 or 2024.1 | [Remove the index table](#remove-a-table-from-replication) corresponding to the constraint from replication | Execute on Target | Execute on Source |

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

- If you are using Colocated tables, you CREATE TABLE on target, then CREATE TABLE on source, making sure that you force the Colocation ID to be identical to that on target.
- If you try to make a DDL change on source and it fails, you must also make the same attempt on target and get the same failure.

Use the following guidance when managing tables and indexes in universes with replication configured.

## Tables

Note: If you are performing application upgrades involving both adding and dropping tables, perform the upgrade in two parts: first add tables, then drop tables.

### Add a table to replication

To ensure that data is protected at all times, set up replication on a new table _before_ starting any workload.

Before adding a table to replication in YugabyteDB Anywhere, refer to [Order of operations](#order-of-operations) for your setup.

Add tables to replication as follows:

1. Navigate to your source and select **xCluster Replication**.
1. Click **Actions** and choose **Select Databases and Tables**.
1. Select the tables and click **Validate Selection**.
1. If data needs to be copied, click **Next: Confirm Full Copy**.
1. Click **Apply Changes**.

Note the following:

- If the newly added table already has data, then adding the table can trigger a full copy of that entire database from source to target.

- It is recommended that you set up replication on the new table before starting any workload to ensure that data is protected at all times. This approach also avoids the full copy.

- This operation also automatically adds any associated index tables of this table to the replication configuration.

- If using colocation, colocated tables on the source and target should be created with the same colocation ID if they already exist on both the source and target prior to replication setup.

### Remove a table from replication

Before dropping a table, refer to [Order of operations](#order-of-operations) for your setup.

When dropping a table in version 2.23 or later, you can drop the tables and YugabyteDB Anywhere automatically updates the xCluster configuration. You can also manually remove the tables from the configuration in YugabyteDB Anywhere, in which case you would still need to drop the tables.

When dropping a table in versions earlier than 2.23, remove the table from replication before dropping the table in the source and target databases, as follows:

1. Navigate to your source and select **xCluster Replication**.
1. Click **Actions** and choose **Select Databases and Tables**.
1. Deselect the tables and click **Validate Selection**.
1. Click **Apply Changes**.

## Indexes

### Add an index to replication

To add an index, refer to [Order of operations](#order-of-operations) for your setup.

For non-transactional YSQL xCluster configurations, adding an index table to a main table that already has data will trigger a [full copy](../xcluster-replication-setup/#full-copy-during-xcluster-setup) of the entire database. To avoid the full copy, you can add the index table manually (see [Adding indexes](../../../../deploy/multi-dc/async-replication/async-deployment/#adding-indexes)), and then [reconcile the configuration](#reconcile-configuration).

For YCQL xCluster configurations, if adding an index table triggers a full copy, the main table and all associated index tables are copied.

### Remove an index from replication

To remove an index, refer to [Order of operations](#order-of-operations) for your setup.

## Reconcile configuration

For some operations, to ensure changes made outside of YugabyteDB Anywhere are reflected in YugabyteDB Anywhere, you need to reconcile the configuration as follows:

1. In YugabyteDB Anywhere, navigate to your source and select **xCluster Replication**.
1. Click **Actions > Advanced** and choose **Reconcile Config with Database**.
