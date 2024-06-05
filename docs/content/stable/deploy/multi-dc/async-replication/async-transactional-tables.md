---
title: Manage tables and indexes transactional xCluster replication
headerTitle: Manage tables and indexes
linkTitle: Tables and indexes
description: Manage tables and indexes using transactional (active-standby) replication between universes
headContent: How to add and remove tables and indexes from replication
menu:
  stable:
    parent: async-replication-transactional
    identifier: async-transactional-tables
    weight: 50
type: docs
---

Use the following guidance when managing tables and indexes in transactional xCluster deployments.

## Add a table to replication

To ensure that data is protected at all times, set up replication on a new table _before_ starting any workload.

If a table already has data before adding it to replication, then adding the table to replication can result in a backup and restore of the entire database from Primary to Standby.

Add tables to replication in the following sequence:

1. Create the table on the Primary (if it doesn't already exist).
1. Create the table on the Standby.
1. Add the table to the replication.

    For instructions on adding tables to replication in YugabyteDB Anywhere, refer to [View, manage, and monitor replication](../../../../yugabyte-platform/create-deployments/async-replication-platform/#view-manage-and-monitor-replication).

## Remove a table from replication

Remove tables from replication in the following sequence:

1. Remove the table from replication.

    For instructions on removing tables from replication in YugabyteDB Anywhere, refer to [View, manage, and monitor replication](../../../../yugabyte-platform/create-deployments/async-replication-platform/#view-manage-and-monitor-replication).

1. Drop the table from both Primary and Standby databases separately.

## Add a new index to replication

Indexes are automatically added to replication in an atomic fashion after you create the indexes separately on Primary and Standby. You do not have to stop the writes on the Primary.

**Note**: The Create Index DDL may kill some in-flight transactions. This is a temporary error. Retry any failed transactions.

Add indexes to replication in the following sequence:

1. Create an index on the Primary.

1. Wait for index backfill to finish.

1. Create the same index on Standby.

1. Wait for index backfill to finish.

    For instructions on monitoring backfill, refer to [Monitor index backfill from the command line](https://yugabytedb.tips/?p=2215).

1. [Resynchronize YBA](#resynchronize-yba).

    This step is only required if you are using YBA, and ensures your changes are reflected in the YugabyteDB Anywhere UI.

## Remove an index from replication

When an index is dropped it is automatically removed from replication.

Remove indexes from replication in the following sequence:

1. Drop the index on the Standby universe.

1. Drop the index on the Primary universe.

1. [Resynchronize YBA](#resynchronize-yba).

    This step is only required if you are using YBA, and ensures your changes are reflected in the YugabyteDB Anywhere UI.

## Add a table partition to the replication

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

## Remove table partitions from replication

To remove a table partition from replication, follow the same steps as [Remove a table from replication](#remove-a-table-from-replication).

## Resynchronize YBA

To ensure changes made outside of YugabyteDB Anywhere are reflected in YBA, resynchronize the YBA UI using the YBA API [sync xCluster config command](https://api-docs.yugabyte.com/docs/yugabyte-platform/e19b528a55430-sync-xcluster-config).

Before you can use the command, you need an API token, your customer ID, and the UUID of the Standby universe; refer to [Automation](../../../../yugabyte-platform/anywhere-automation/).

To resynchronize the YBA UI, run the following on the command line on the YBA host:

```sh
curl -k --location --request POST 'https://<yourportal.yugabyte.com>/api/v1/customers/<Customer_ID>/xcluster_configs/sync?targetUniverseUUID=<standby_universe_uuid>' --header 'X-AUTH-YW-API-TOKEN: <API_token>' --data ''
```
