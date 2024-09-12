---
title: Manage tables and indexes for xCluster Replication
headerTitle: Manage tables and indexes
linkTitle: Tables and indexes
description: Manage tables and indexes in universes in xCluster Replication
headContent: Add and remove tables and indexes in universes with xCluster Replication
menu:
  stable_yugabyte-platform:
    parent: xcluster-replication
    identifier: xcluster-replication-ddl
    weight: 30
type: docs
---

When making DDL changes to databases in xCluster Replication (such as creating, altering, or dropping tables or partitions), you must do the following:

- Make the change at the SQL level on both the source and target.
- Update the xCluster Replication configuration in YugabyteDB Anywhere.

The order in which you do this varies depending on the operation.

## Order of operations

Perform DDL operations in the order as shown in the following table. The order varies depending on the [xCluster configuration](../#xcluster-configurations).

{{<tabpane text=true >}}
{{% tab header="YSQL Transactional" lang="ysql-transaction" %}}

| DDL | Step 1 | Step 2 | Step 3 |
| :-- | :----- | :----- | :----- |
| CREATE TABLE | Execute on Source | Execute on Target | [Add table to replication](#add-a-table-to-replication) |
| DROP TABLE | [Remove table from replication](#remove-a-table-from-replication) | Execute on Target | Execute on Source |
| CREATE TABLE foo<br>PARTITION OF bar | Execute on Source | Execute on Target | [Add table to replication](#add-a-table-to-replication) |
| CREATE TABLE<br>Colocated | Execute on Target | Execute on Source using same colocation ID | No changes needed |
| CREATE INDEX | Execute on Source<br>wait for backfill | Execute&nbsp;on&nbsp;Target<br>wait for backfill | [Reconcile configuration](#reconcile-configuration) |
| DROP INDEX   | Execute on Target | Execute on Source | [Reconcile configuration](#reconcile-configuration) |
| ALTER TABLE or INDEX | Execute&nbsp;on&nbsp;Target | Execute on Source | No changes needed |
| ALTER TABLE<br>ADD CONSTRAINT UNIQUE | Execute on Source<br>wait for backfill | Execute on Target<br>wait for backfill | [Reconcile configuration](#reconcile-configuration) |
| ALTER TABLE<br>DROP&nbsp;CONSTRAINT<br>(unique constraints only) | Execute on Target | Execute on Source | [Reconcile configuration](#reconcile-configuration) |

{{% /tab %}}
{{% tab header="YSQL Non-Transactional" lang="ysql-nontransaction" %}}

| DDL | Step 1 | Step 2 | Step 3 |
| :-- | :----- | :----- | :----- |
| CREATE TABLE | Execute on Source | Execute on Target | [Add table to replication](#add-a-table-to-replication) |
| DROP TABLE | [Remove table from replication](#remove-a-table-from-replication) | Execute on Target | Execute&nbsp;on&nbsp;Source |
| CREATE TABLE foo<br>PARTITION OF bar | Execute&nbsp;on&nbsp;Source | Execute on Target | [Add table to replication](#add-a-table-to-replication) |
| CREATE TABLE<br>Colocated | Execute on Target | Execute&nbsp;on&nbsp;Source using same colocation ID | No changes needed |
| CREATE INDEX | Execute on Source<br>wait for backfill | Execute&nbsp;on&nbsp;Target<br>wait for backfill | [Add index table to replication](#add-a-table-to-replication) |
| DROP INDEX | [Remove index table from replication](#remove-a-table-from-replication) | Execute on Target | Execute on Source |
| ALTER TABLE or INDEX | Execute&nbsp;on&nbsp;Target | Execute on Source | No changes needed |
| ALTER TABLE<br>ADD CONSTRAINT UNIQUE | Execute on Source<br>wait for backfill | Execute on Target<br>wait for backfill | [Add the index table](#add-a-table-to-replication) corresponding to the constraint to replication |
| ALTER TABLE<br>DROP&nbsp;CONSTRAINT<br>(unique constraints only) | [Remove the index table](#remove-a-table-from-replication) corresponding to the constraint from replication | Execute on Target | Execute on Source |

{{% /tab %}}
{{% tab header="YCQL" lang="ycql" %}}

| DDL | Step 1 | Step 2 | Step 3 |
| :-- | :----- | :----- | :----- |
| CREATE TABLE | Execute on Source | Execute on Target | [Add table to replication](#add-a-table-to-replication) |
| DROP TABLE   | [Remove table from replication](#remove-a-table-from-replication) | Execute on Target | Execute on Source |
| CREATE INDEX | Execute on Source | Execute&nbsp;on&nbsp;Target | [Restart replication for the main table](../xcluster-replication-setup/#restart-replication) |
| DROP INDEX   | [Remove index table from replication](#remove-a-table-from-replication) | Execute on Target | Execute on Source |
| ALTER TABLE or INDEX | Execute&nbsp;on&nbsp;Target | Execute&nbsp;on&nbsp;Source | No changes needed |

{{% /tab %}}
{{</tabpane >}}

In addition, keep in mind the following:

- If you are using Colocated tables, you CREATE TABLE on target, then CREATE TABLE on source, making sure that you force the Colocation ID to be identical to that on target.
- If you try to make a DDL change on source and it fails, you must also make the same attempt on target and get the same failure.
- TRUNCATE TABLE is not supported. To truncate a table, pause replication, truncate the table on both source and target, and resume replication.

Use the following guidance when managing tables and indexes in universes with replication configured.

## Tables

Note: If you are performing application upgrades involving both adding and dropping tables, perform the upgrade in two parts: first add tables, then drop tables.

### Add a table to replication

To avoid a [full copy](../xcluster-replication-setup/#full-copy-during-xcluster-setup), add the table to replication when it is newly created (that is, empty) on both source and target.

{{<tip>}}
Before adding a table to replication in YugabyteDB Anywhere, refer to [Order of operations](#order-of-operations) for your setup.
{{</tip>}}

Add tables to replication as follows:

1. In YugabyteDB Anywhere, navigate to your source, select **xCluster Replication**, and select the replication configuration.
1. Click **Actions** and choose **Select Databases and Tables**.
1. Select the tables and click **Validate Selection**.

    For YSQL, select the database(s) with the tables you want in replication. Colocated tables require additional conditions. For more information, see [YSQL tables](../xcluster-replication-setup/#ysql-tables).

1. If data needs to be copied, click **Next: Confirm Full Copy**.
1. Click **Apply Changes**.

This operation also automatically adds any associated index tables of this table to the replication configuration.

### Remove a table from replication

{{<tip>}}
Before dropping a table in replication in YugabyteDB Anywhere, refer to [Order of operations](#order-of-operations) for your setup.
{{</tip>}}

When dropping a table, remove the table from replication before dropping the table in the source and target databases, as follows:

1. In YugabyteDB Anywhere, navigate to your source, select **xCluster Replication**, and select the replication configuration.
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

1. In YugabyteDB Anywhere, navigate to your source, select **xCluster Replication**, and select the replication configuration.
1. Click **Actions > Advanced** and choose **Reconcile Config with Database**.
