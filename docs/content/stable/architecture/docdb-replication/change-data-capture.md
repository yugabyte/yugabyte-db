---
title: Change data capture (CDC) gRPC Replication in YugabyteDB
headerTitle: CDC using gRPC Replication
linkTitle: CDC using gRPC Replication
description: Learn how YugabyteDB supports asynchronous replication of data changes (inserts, updates, and deletes) to external databases or applications.
badges: ea
menu:
  stable:
    parent: architecture-docdb-replication
    identifier: architecture-docdb-replication-cdc
    weight: 600
type: docs
---

## Architecture

Every YB-TServer has a `CDC service` that is stateless. The main APIs provided by the CDC service are the following:

- `createCDCSDKStream` API for creating the stream on the database.
- `getChangesCDCSDK` API that can be used by the client to get the latest set of changes.

![Stateless CDC Service](/images/architecture/stateless_cdc_service.png)

## CDC streams

YugabyteDB automatically splits user tables into multiple shards (also called tablets) using either a hash- or range-based strategy. The primary key for each row in the table uniquely identifies the location of the tablet in the row.

Each tablet has its own WAL file. WAL is NOT in-memory, but it is disk persisted. Each WAL preserves the order in which transactions (or changes) happened. Hybrid TS, Operation ID, and additional metadata about the transaction is also preserved.

![How does CDC work](/images/explore/cdc-overview-work2.png)

YugabyteDB normally purges WAL segments after some period of time. This means that the connector does not have the complete history of all changes that have been made to the database. Therefore, when the connector first connects to a particular YugabyteDB database, it starts by performing a consistent snapshot of each of the database schemas.

The YugabyteDB Debezium connector captures row-level changes in the schemas of a YugabyteDB database. The first time it connects to a YugabyteDB cluster, the connector takes a consistent snapshot of all schemas. After that snapshot is complete, the connector continuously captures row-level changes that insert, update, and delete database content, and that were committed to a YugabyteDB database.

![How does CDC work](/images/explore/cdc-overview-work.png)

The core primitive of CDC is the _stream_. Streams can be enabled and disabled on databases. You can specify which tables to include or exclude. Every change to a watched database table is emitted as a record in a configurable format to a configurable sink. Streams scale to any YugabyteDB cluster independent of its size and are designed to impact production traffic as little as possible.

Creating a new CDC stream returns a stream UUID. This is facilitated via the [yb-admin](../../../admin/yb-admin/#change-data-capture-cdc-commands) tool. A stream ID is created first, per database. You configure the maximum batch side in YugabyteDB, while the polling frequency is configured on the connector side.

Connector tasks can consume changes from multiple tablets. At least once delivery is guaranteed. In turn, connector tasks write to the Kafka cluster, and tasks don't need to match Kafka partitions. Tasks can be independently scaled up or down.

The connector produces a change event for every row-level insert, update, and delete operation that was captured, and sends change event records for each table in a separate Kafka topic. Client applications read the Kafka topics that correspond to the database tables of interest, and can react to every row-level event they receive from those topics. For each table, the default behavior is that the connector streams all generated events to a separate Kafka topic for that table. Applications and services consume data change event records from that topic. All changes for a row (or rows in the same tablet) are received in the order in which they happened. A checkpoint per stream ID and tablet is updated in a state table after a successful write to Kafka brokers.

## CDC guarantees

CDC in YugabyteDB provides technology to ensure that any changes in data due to operations (such as inserts, updates, and deletions) are identified, captured, and automatically applied to another data repository instance, or made available for consumption by applications and other tools. CDC provides the following guarantees.

### Per-tablet ordered delivery

All data changes for one row or multiple rows in the same tablet are received in the order in which they occur. Due to the distributed nature of the problem, however, gRPC replication does not guarantee order across tablets.

Consider the following scenario:

- Two rows are being updated concurrently.
- These two rows belong to different tablets.
- The first row `row #1` was updated at time `t1`, and the second row `row #2` was updated at time `t2`.

In this case, it is possible for CDC to push the later update corresponding to `row #2` change to Kafka before pushing the earlier update, corresponding to `row #1`.

### At-least-once delivery

Updates for rows are pushed at least once. With the at-least-once delivery, you never lose a message, however the message might be delivered to a CDC consumer more than once. This can happen in case of a tablet leader change, where the old leader already pushed changes to Kafka, but the latest pushed `op id` was not updated in the CDC metadata.

For example, a CDC client has received changes for a row at times `t1` and `t3`. It is possible for the client to receive those updates again.

### No gaps in change stream

When you have received a change for a row for timestamp `t`, you do not receive a previously unseen change for that row from an earlier timestamp. This guarantees that receiving any change implies that all earlier changes have been received for a row.

{{< note title="Note" >}}

See [Change data capture](../../../explore/change-data-capture/) in Explore for more details and limitations.

{{< /note >}}
