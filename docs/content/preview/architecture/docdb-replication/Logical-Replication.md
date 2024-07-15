---
title: Logical Replication in YugabyteDB
headerTitle: Logical Replication
linkTitle: Logical Replication
description: Learn how YugabyteDB supports asynchronous replication of data changes (inserts, updates, and deletes) to external databases or applications.
badges: ea
menu:
  preview:
    parent: architecture-docdb-replication
    identifier: architecture-docdb-replication-logical-replication
    weight: 500
type: docs
---

Change data capture (CDC) in YugabyteDB provides technology to ensure that any changes in data due to operations such as inserts, updates, and deletions are identified, captured, and made available for consumption by applications and other tools. 

CDC in YugabyteDB is based on the Postgres Logical Replication model. The fundamental concept here is that of the Replication Slot. A Replication Slot represents a stream of changes that can be replayed to the client in the order they were made on the origin server in a manner that preserves transactional consistency. This is the basis for the support for Transactional CDC in YugabyteDB. Where the strict requirements of Transactional CDC are not present, multiple replication slots can be used to stream changes from unrelated tables in parallel.


## Architecture<a id="architecture-1"></a>

![Logical-Replication-Architecture](/images/architecture/logical_replication_architecture.png)

The following are the main components of the Yugabyte CDC solution -

1. Walsender - A special purpose PG backend responsible for streaming changes to the client and handling acknowledgments.

2. Virtual WAL (VWAL) - Assembles changes from all the tablets of tables (under the publication) to maintain transactional consistency.

3. CDCService - Retrieves changes from the WAL of a specified shard/tablet starting from a given checkpoint.


### Data Flow<a id="data-flow"></a>

Logical replication starts by copying a snapshot of the data on the publisher database. Once that is done, changes on the publisher are streamed to the server as they occur in near real time.


#### Initial Snapshot<a id="initial-snapshot"></a>
TBD



#### Streaming Data Flow<a id="streaming-data-flow"></a>

YugabyteDB automatically splits user tables into multiple shards (also called tablets) using either a hash- or range-based strategy. The primary key for each row in the table uniquely identifies the location of the tablet in the row.

Each tablet has its own WAL. WAL is NOT in-memory, but it is disk persisted. Each WAL preserves the information on the changes involved in the transactions (or changes) for that tablet as well as additional metadata related to the transactions.

Step 1 - Data flow from the tablets’ WAL to the VWAL

![CDCService-VWAL](/images/architecture/cdc_service_vwal_interaction.png)

Each tablet sends changes in transaction commit time order. Further, within a transaction, the changes are in the order in which the operations were performed in the transaction.

Step 2 - Sorting in the VWAL and sending transactions to the Walsender

![VWAL-Walsender](/images/architecture/vwal_walsender_interaction.png)

VWAL collects changes across multiple tablets, assembles the transactions, assigns LSN to each change and transaction boundary (BEGIN, COMMIT) record and sends the changes to the Walsender in transaction commit time order.

Step 3 - Walsender to client

Walsender sends changes to the output plugin, which filters them according to the slot's publication and converts them into the client's desired format. These changes are then streamed to the client using the appropriate streaming replication protocols determined by the output plugin. Yugabyte follows the same streaming replication protocols as defined in PostgreSQL.

{{< note title="Note" >}}
Please refer to the official PostgreSQL documentation for [Streaming Replication Protocol](https://www.postgresql.org/docs/11/protocol-replication.html) & [Logical Streaming Replication Protocol](https://www.postgresql.org/docs/11/protocol-logical-replication.html).

{{< /note >}}



{{< note title="Note" >}}

See [Logical Replication](../../../explore/logical-replication/) in Explore to setup CDC.

{{< /note >}}
