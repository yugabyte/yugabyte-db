---
title: Manage tables and indexes transactional xCluster replication
headerTitle: Manage tables and indexes
linkTitle: Tables and indexes
description: Manage tables and indexes using transactional (active-standby) replication between universes
headContent: Add and remove tables and indexes
menu:
  preview:
    parent: async-replication-transactional
    identifier: async-transactional-tables
    weight: 50
type: docs
---

## Add a table to replication

1. Create table on the Primary (if it does not already exist).
1. Create table on the Standby.
1. Add the table to replication using YBA UI.

If the newly added table already has data before adding it to replication, then adding the table to replication can result in a Backup/Restore of that entire database from Primary to Standby.

**Note**: The recommendation is to always set up replication on the new table before starting any workload to ensure that data is protected at all times.

## Remove a table from replication

1. Remove the table from replication using the YBA UI.

1. Drop the table from both Primary and Standby databases separately.

## Add a new index to replication

Indexes are automatically added to replication in an atomic fashion after you create the indexes separately on Primary and Standby. You do not have to stop the writes on the Primary clusters.

**Note**: The Create Index DDL may kill some in-flight transactions. This is a temporary error. Retry any failed transactions.

1. Create an index on the Primary.

1. Wait for index backfill to finish.

1. Create the same index on Standby.

1. Wait for index backfill to finish.

1. [Manually resync YBA monitoring](#manually-resync-yba-monitoring).

For instructions on monitoring backfill, refer to [Monitor index backfill from the command line](https://yugabytedb.tips/?p=2215).

## Remove an index from replication

When an index is dropped it is automatically removed from replication.

1. Drop the index on the Standby universe.

1. Drop the index on the Primary universe.

1. [Manually resync YBA monitoring](#manually-resync-yba-monitoring).

## Add a table partition to the replication

The operation is the same as adding a table.

The caveat is that the parent table (if not already) along with each new partition will have to be added to the replication as DDL changes are not replicated automatically. Each partition is treated as a separate table and is added to replication separately (like a table)

Create a table with partitions:

```sql
CREATE TABLE order_changes (
  order_id int,
  change_date date,
  type text,
  description text)
  PARTITION BY RANGE (change_date);  

CREATE TABLE order_changes_default PARTITION OF order_changes DEFAULT;
```

Create a new partition:

```sql
CREATE TABLE order_changes_2023_01 PARTITION OF order_changes
FOR VALUES FROM ('2023-01-01') TO ('2023-03-30');
```

Assume the parent table and default partition are included in the replication stream.

To add a table partition to the replication, follow the same steps for [Add Table to replication](#add-table-to-replication).

## Remove table partition from replication

Remove Partition from Replication is the same as [Remove Table from Replication](#remove-table-from-replication).

## Manually resync YBA monitoring

One time setup:

1. In YugabyteDB Anywhere, navigate to your profile by clicking the profile icon in the top right and choosing **User Profile** (that is, https://<yourportal.yugabyte.com>/profile).

1. Click **Generate Key**.

1. Copy the API Token and Customer ID and store it in a secure location.

1. Navigate to the Standby universe and get the cluster UUID from the URL.

1. Run the following command:

    ```sh
    curl -k --location --request POST 'https://<yourportal.yugabyte.com>/api/v1/customers/<CutomerID>/xcluster_configs/sync?targetUniverseUUID=<StandbyUniverseUUID>' --header 'X-AUTH-YW-API-TOKEN: <APIToken>' --data
    ```
