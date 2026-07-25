---
title: Architecture for CDC using PostgreSQL protocol
headerTitle: CDC using PostgreSQL protocol
linkTitle: CDC using PostgreSQL protocol
description: Learn how YugabyteDB supports asynchronous replication of data changes (inserts, updates, and deletes) to external databases or applications.
headContent: Asynchronous replication of data changes (inserts, updates, and deletes) to external databases or applications
tags:
  feature: early-access
aliases:
  - /stable/explore/logical-replication/
menu:
  stable:
    parent: architecture-docdb-replication
    identifier: architecture-docdb-replication-cdc-logical-replication
    weight: 500
type: docs
---

Change data capture (CDC) in YugabyteDB provides technology to ensure that any changes in data due to operations such as inserts, updates, and deletions are identified, captured, and made available for consumption by applications and other tools.

CDC using PostgreSQL protocol in YugabyteDB is based on the PostgreSQL Logical Replication model. The fundamental concept is that of the Replication Slot. A Replication Slot represents a stream of changes that can be replayed to the client in the order they were made on the origin server in a manner that preserves transactional consistency. This is the basis for the support for Transactional CDC in YugabyteDB. Where the strict requirements of Transactional CDC are not present, multiple replication slots can be used to stream changes from unrelated tables in parallel.

{{<lead link="../../../additional-features/change-data-capture/">}}

See [Change data capture](../../../additional-features/change-data-capture/) for more details and limitations.

{{</lead>}}

## Architecture

![Logical replication architecture](/images/architecture/cdc-logical-replication-architecture.png)

The following are the main components of the Yugabyte CDC solution:

1. CDC Service. Retrieves changes from the WAL of a specified shard starting from a given checkpoint. When [implicit publication](../../../additional-features/change-data-capture/using-logical-replication/advanced-topic/#implicit-publication) is enabled (the default), the CDC service runs on both YB-TServer and YB-Master.

2. Virtual WAL (VWAL). Assembles changes from all the shards of user tables (under the publication) to maintain transactional consistency.

3. walsender. A special purpose PostgreSQL backend responsible for streaming changes to the client and handling acknowledgments.

### Detecting publication changes

Starting in v2026.1, the virtual WAL polls the sys catalog tablet in addition to the tablets of user tables. As a result, any DDL that changes the content of catalog tables (such as `ALTER PUBLICATION`) can be detected by the virtual WAL in the correct consistent order of commit time. This allows publication changes to be reflected in the replication stream at the same point in time as in PostgreSQL, without relying on periodic publication list refresh.

In versions earlier than v2026.1, the CDC service runs only on YB-TServer, and publication changes are detected through periodic polling of the publication's tables list.

### Table rewrite and DROP TABLE handling

When [streaming DDLs that cause table rewrite](../../../additional-features/change-data-capture/using-logical-replication/advanced-topic/#streaming-ddls-causing-table-rewrite) is enabled (the default), a DDL that causes a table rewrite or a `DROP TABLE` on a database with active CDC does not immediately delete the table's tablets. Instead, those tablets are hidden and retained until CDC has streamed all data committed before the DDL.

CDC continues to serve changes from the hidden tablets to the client until that data is fully streamed. When the virtual WAL receives records indicating a DDL that causes a table rewrite, it switches polling to the new (re-written) tablets. For `DROP TABLE`, it removes the tablets from its polling list.

The restart time is the latest commit time acknowledged by the CDC client. When the restart time across all replication slots passes the hide time of these tablets, the tablets are marked as available for deletion and are subsequently deleted.

In versions earlier than v2026.1, such DDLs are blocked when logical replication is active on the database.

### Data flow

Logical replication starts by copying a snapshot of the data on the publisher database. After that is done, changes on the publisher are streamed to the server as they occur in near real time.

To set up Logical Replication, an application will first have to create a replication slot. When a replication slot is created, a boundary is established between the snapshot data and the streaming changes. This boundary or `consistent_point` is a consistent state of the source database. It corresponds to a commit time (HybridTime value). Data from transactions with commit time <= commit time corresponding to the `consistent_point` are consumed as part of the initial snapshot. Changes from transactions with commit time greater than the commit time of the `consistent_point` are consumed in the streaming phase in transaction commit time order.

#### Initial snapshot

The initial snapshot data for each table is consumed by executing a corresponding snapshot query (SELECT statement) on that table. This snapshot query should be executed as of the database state corresponding to the `consistent_point`. This database state is represented by a value of HybridTime.

First, a `SET LOCAL yb_read_time TO '<consistent_point commit time> ht'` command should be executed on the connection (session). The SELECT statement corresponding to the snapshot query should then be executed as part of the same transaction.

The HybridTime value to use in the `SET LOCAL yb_read_time` command is the value of the `snapshot_name` field that is returned by the `CREATE_REPLICATION_SLOT` command. Alternatively, it can be obtained by querying the `pg_replication_slots` view.

During Snapshot consumption, the snapshot data from all tables will be from the same consistent state (`consistent_point`). At the end of Snapshot consumption, the state of the target system is at/based on the `consistent_point`. History of the tables as of the `consistent_point` is retained on the source until the snapshot is consumed.

#### Streaming data flow

YugabyteDB automatically splits user tables into multiple shards (also called tablets) using either a hash- or range-based strategy. The primary key for each row in the table uniquely identifies the location of the tablet in the row.

Each tablet has its own WAL. WAL is NOT in-memory, but it is disk persisted. Each WAL preserves the information on the changes involved in the transactions (or changes) for that tablet as well as additional metadata related to the transactions.

**Step 1 - Data flow from the tablet WAL to the VWAL**

![CDCService-VWAL](/images/architecture/cdc_service_vwal_interaction.png)

Each tablet sends changes in transaction commit time order. Further, in a transaction, the changes are in the order in which the operations were performed in the transaction.

**Step 2 - Sorting in the VWAL and sending transactions to the walsender**

![VWAL-walsender](/images/architecture/vwal_walsender_interaction.png)

VWAL collects changes across multiple tablets, assembles the transactions, assigns a Log Sequence Number ([LSN](../../../additional-features/change-data-capture/using-logical-replication/key-concepts/#lsn-type)) to each change and transaction boundary (BEGIN, COMMIT) record, and sends the changes to the walsender in transaction commit time order.

**Step 3 - walsender to client**

The walsender sends changes to the output plugin, which filters them according to the slot's publication and converts them into the client's desired format. These changes are then streamed to the client using the appropriate streaming replication protocols determined by the output plugin. Yugabyte follows the same streaming replication protocols as defined in PostgreSQL.

<!--TODO (Siddharth): Fix the Links to the protocol section.

{{< note title="Note" >}}
Refer to [Replication Protocol](../../../additional-features/change-data-capture/using-logical-replication/#streaming-protocol) for more details.

{{< /note >}}

{{< tip title="Explore" >}}

See [Getting Started with Logical Replication](../../../additional-features/change-data-capture/using-logical-replication/getting-started/) to set up Logical Replication in YugabyteDB.

{{< /tip >}}
-->
