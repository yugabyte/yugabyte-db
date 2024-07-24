---
title: Using logical replication
headerTitle: Using logical replication
linkTitle: Using logical replication
description: CDC or Change data capture is a process to capture changes made to data in the database.
headcontent: Capture changes made to data in the database
image: /images/section_icons/index/develop.png
cascade:
  earlyAccess: /preview/releases/versioning/#feature-maturity
menu:
  preview:
    identifier: explore-change-data-capture-logical-replication
    parent: explore-change-data-capture
    weight: 240
type: indexpage
---

## Overview

YugabyteDB CDC captures changes made to data in the database and streams those changes to external processes, applications, or other databases. CDC allows you to track and propagate changes in a YugabyteDB database to downstream consumers based on its Write-Ahead Log (WAL). YugabyteDB CDC captures row-level changes resulting from INSERT, UPDATE, and DELETE operations in the configured database and publishes it further to be consumed by downstream applications.

## Key concepts

The YugabyteDB logical replication feature makes use of PostgreSQL concepts like replication slot, publication, replica identity, and so on. Understanding these key concepts is crucial for setting up and managing a logical replication environment effectively.

### Replication slot

A replication slot represents a stream of changes that can be replayed to a client in the order they were made on the origin server. Each slot streams a sequence of changes from a single database.

In logical replication, the fundamental unit of data transmission is a transaction. A logical slot emits each change just once in normal operation. The current position of each slot is persisted only at checkpoint, so if a replication process is interrupted and restarts, even if the checkpoint or the starting LSN falls in the middle of a transaction, **the entire transaction is retransmitted**. This behavior guarantees that clients receive complete transactions without missing any intermediate changes, maintaining data integrity across the replication stream​. Logical decoding clients are responsible for avoiding ill effects from handling the same message more than once. Clients may wish to record the last LSN they saw when decoding and skip over any repeated data or (when using the replication protocol) request that decoding start from that LSN rather than letting the server determine the start point.

For more information, refer to [Replication slots](https://www.postgresql.org/docs/11/logicaldecoding-explanation.html#LOGICALDECODING-REPLICATION-SLOTS) in the PostgreSQL documentation.

### Publication

A publication is a set of changes generated from a table or a group of tables, and might also be described as a change set or replication set. Each publication exists in only one database.

Publications are different from schemas and do not affect how the table is accessed. Each table can be added to multiple publications if needed. Publications may currently only contain tables. Objects must be added explicitly, except when a publication is created for ALL TABLES.

For more information, refer to [Publication](https://www.postgresql.org/docs/11/logical-replication-publication.html#LOGICAL-REPLICATION-PUBLICATION) in the PostgreSQL documentation.

### Output plugin

Output plugins transform the data from the write-ahead log's internal representation into the format that can be consumed by replication clients. These plugins are notified about the change events that need to be processed and sent via various callbacks. These callbacks are only invoked when the transaction actually commits.

YugabyteDB supports the following four output plugins:

- `yboutput`
- `pgoutput`
- `test_decoding`
- `wal2json`

All these plugins are pre-packaged with YugabyteDB and do not require any external installation.

For more information, refer to [Logical Decoding Output Plugins](https://www.postgresql.org/docs/11/logicaldecoding-output-plugin.html) in the PostgreSQL documentation.

### LSN

LSN (Log Sequence Number) in YugabyteDB is an unsigned 64-bit integer that uniquely identifies a change record or a transaction boundary record that is consumed from a given replication slot.

In YugabyteDB, LSN values from different slots are considered unrelated and should not be compared. In YugabyteDB, LSN no longer represents the byte offset of a WAL record.

LSN values for a single replication slot satisfy the following properties:

- **Uniqueness**

    LSN values for the change and `COMMIT` records for a given replication slot are unique. In particular, changes from different tablets of the same or different tables will have unique LSN values for a replication slot.

- **Ordering**

    LSN values can be compared ( `<`, `>`, `=` ).

    The LSN of the change records in a transaction will be strictly lower than the LSN of the COMMIT record of the same transaction.

    The LSNs of change records in a transaction will be in increasing order and will correspond to the order in which those changes were made in that transaction. That is, the LSN of an earlier change will have a strictly lower value than the LSN of a later change in the same transaction. This is the case even if the changes correspond to rows in different tablets of the same or different tables.

    For a given replication slot, the LSN of a `COMMIT` record of an earlier transaction will be strictly lower than the LSN of the `COMMIT` record of a later transaction.

- **Determinism**

    For a given replication slot, the LSN value of a change record (or a transaction boundary record) remains the same for the lifetime of that replication slot. In particular, this is true across server and client restarts and client re-connections. Thus, LSN values for a single replication slot may be used to uniquely identify records that are consumed from that replication slot. The values can be compared for determining duplicates at the client side.

### Replica identity

Replica identity is a table-level parameter that controls the amount of information being written to the change records. YugabyteDB supports the following four replica identities:

- CHANGE (default)
- DEFAULT
- FULL
- NOTHING

The replica identity `INDEX` is not supported in YugabyteDB.

Replica identity `CHANGE` is the best performant and the default replica identity. The replica identity of a table can be changed by performing an alter table. However, for a given slot, the alter tables performed to change the replica identity after the creation of the slot will have no effect. This means that the effective replica identity for any table for a slot, is the replica identity of the table that existed at the time of slot creation. A dynamically created table (a table created after slot creation) will have the default replica identity. For a replica identity modified after slot creation to take effect, a new slot will have to be created after performing the Alter table.

The [ysql_yb_default_replica_identity](../../../../reference/configuration/yb-tserver/#ysql-yb-default-replica-identity) flag determines the default replica identity for user tables at the time of table creation. The default value is `CHANGE`. The purpose of this flag is to set the replica identities for dynamically created tables. In order to create a dynamic table with desired replica identity, the flag must be set accordingly and then the table must be created.

{{< note title="Advisory" >}}
You should refrain from altering the replica identity of a dynamically created table for at least 5 minutes after its creation.
{{< /note >}}

For more information, refer to [Replica Identity](https://www.postgresql.org/docs/11/sql-altertable.html#SQL-CREATETABLE-REPLICA-IDENTITY) in the PostgreSQL documentation.

### Replication protocols

PostgreSQL has defined protocols for replication that need to be followed by clients to establish replication connection as well as message structures for streaming data. This includes the [Streaming Replication protocol](https://www.postgresql.org/docs/11/protocol-replication.html) and the [Logical Streaming Replication protocol](https://www.postgresql.org/docs/11/protocol-logical-replication.html).

The logical streaming replication protocol sends individual transactions one-by-one. This means that all messages between a pair of `BEGIN` and `COMMIT` messages belong to the same transaction.

YugabyteDB supports both the streaming replication protocols used in PostgreSQL to support logical replication, maintaining the same semantics described in PostgreSQL:

- Streaming Replication Protocol - This protocol is followed by all output plugins.

- Logical Streaming Replication Protocol - This protocol is followed by `pgoutput` and `yboutput`, in addition to the Streaming replication protocol.

{{< note title="Note" >}}

YugabyteDB does not support Physical Replication.

{{< /note >}}

## Guarantees

| GUARANTEE | DESCRIPTION |
| :----- | :----- |
| Per-slot ordered delivery guarantee | Changes from transactions from all the tables that are part of the replication slot’s publication are received in the order they were committed. This also implies ordered delivery across all the tablets that are part of the publication’s table list. |
| At least once delivery | Changes from transactions are streamed at least once. Changes from transactions may be streamed again in case of restart after failure. For example, this can happen in the case of a Kafka Connect node failure. If the Kafka Connect node pushes the records to Kafka and crashes before committing the offset, it will again get the same set of records upon restart. |
| No gaps in change stream | Receiving changes that are part of a transaction with commit time *t* implies that you have already received changes from all transactions with commit time lower than *t*. Thus, receiving any change for a row with commit timestamp *t*,  implies that you have received all older changes for that row. |

## Highlights

### Resilience

1. Following a failure of the application or server or n/w, the replication can continue from any of the available server nodes.

2. Replication continues from the transaction immediately after the transaction that was last acknowledged by the application. There will be no transaction that will be missed by the application.

### Security

CDC in YugabyteDB being based on the PostgreSQL Logical Replication model means:

1. CDC user persona will be a 'PG Replication client'.

2. A standard replication connection is used for consumption, and all the server-side configurations of authentication, authorizations, SSL modes, and connection load balancing can be leveraged automatically.

## Getting started

Get started with YugabyteDB logical replication.

{{<lead link="./get-started">}}
To learn how get started with the connector, see [Get started](./get-started).
{{</lead>}}

## Monitoring

You can monitor the activities and status of the deployed connectors using the http end points provided by YugabyteDB.

{{<lead link="./monitor">}}
To know more about how to monitor your CDC setup, see [Monitor](./monitor).
{{</lead>}}

## Limitations

- LSN Comparisons Across Slots.

    In the case of YugabyteDB, the LSN  does not represent the byte offset of a WAL record. Hence, arithmetic on LSN and any other usages of the LSN making this assumption will not work. Also, currently, comparison of LSN values from messages coming from different replication slots is not supported.

- The following functions are currently unsupported:

  - `pg_current_wal_lsn`
  - `pg_wal_lsn_diff`
  - `IDENTIFY SYSTEM`
  - `txid_current`
  - `pg_stat_replication`

  Additionally, the functions responsible for pulling changes instead of the server streaming it are unsupported as well. They are described in [Replication Functions](https://www.postgresql.org/docs/11/functions-admin.html#FUNCTIONS-REPLICATION) in the PostgreSQL documentation.

- Restriction on DDLs

    DDL operations should not be performed from the time of replication slot creation till the start of snapshot consumption of the last table.

- There should be a primary key on the table you want to stream the changes from.

- CDC is not supported on a target table for xCluster replication [11829](https://github.com/yugabyte/yugabyte-db/issues/11829).

- Currently we don't support schema evolution for changes that require table rewrites (ex: ALTER TYPE).

- YCQL tables aren't currently supported. Issue [11320](https://github.com/yugabyte/yugabyte-db/issues/11320).

- Support for point-in-time recovery (PITR) is tracked in issue [10938](https://github.com/yugabyte/yugabyte-db/issues/10938).

- Support for transaction savepoints is tracked in issue [10936](https://github.com/yugabyte/yugabyte-db/issues/10936).

- Support for enabling CDC on Read Replicas is tracked in issue [11116](https://github.com/yugabyte/yugabyte-db/issues/11116).