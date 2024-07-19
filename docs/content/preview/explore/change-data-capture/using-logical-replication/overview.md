---
title: Overview of CDC - logical replication
linkTitle: Overview
description: Change Data Capture in YugabyteDB.
headcontent: Change Data Capture in YugabyteDB
menu:
  preview:
    parent: explore-change-data-capture-logical-replication
    identifier: overview
    weight: 10
type: docs
---

## Concepts

YugabyteDB’s logical replication feature makes use of the concepts like replication slot, publication, replica identity etc. from Postgres. Understanding these key concepts is crucial for setting up and managing a logical replication environment effectively.

- Replication slot

  - A replication slot represents a stream of changes that can be replayed to a client in the order they were made on the origin server. Each slot streams a sequence of changes from a single database. See [Postgres documentation](https://www.postgresql.org/docs/current/logicaldecoding-explanation.html#LOGICALDECODING-REPLICATION-SLOTS) for more details.

- Publication

  - A publication is a set of changes generated from a table or a group of tables, and might also be described as a change set or replication set. Each publication exists in only one database. See [Postgres documentation](https://www.postgresql.org/docs/current/logical-replication-publication.html#LOGICAL-REPLICATION-PUBLICATION) for more details.

- Output Plugins

  - Output plugins transform the data from the write-ahead log's internal representation into the format the consumer of a replication slot desires. See [Postgres documentation](https://www.postgresql.org/docs/current/logicaldecoding-output-plugin.html) for more details.

- LSN

  - An unsigned 64-bit integer that uniquely identifies a change record or a transaction boundary record that is consumed from a given replication slot.

{{< note title="Note" >}}

In YugabyteDB, LSN values from different slots are considered unrelated and should not be compared. 

{{< /note >}}

- Replica identity

  - Replica identity is a table level parameter that can be used to control the amount of information being written to the change records. See [Postgres documentation](https://www.postgresql.org/docs/current/sql-altertable.html#SQL-ALTERTABLE-REPLICA-IDENTITY) for more details.

- Replication protocols

  - Postgres has defined protocols for replication that need to be followed by clients to establish replication connection as well as message structures for streaming data. This includes two protocols - Streaming Replication protocol ([PG documentation](https://www.postgresql.org/docs/11/protocol-replication.html)), Logical Streaming Replication protocol ([PG documentation](https://www.postgresql.org/docs/11/protocol-logical-replication.html)).


The following sections explain each of the above concepts in detail:

### REPLICATION SLOT

A replication slot represents a stream of changes that can be replayed to a client in the order they were made on the origin server. Each slot streams a sequence of changes from a single database.

In logical replication, the fundamental unit of data transmission is a transaction. A logical slot emits each change just once in normal operation. The current position of each slot is persisted only at checkpoint, so if a replication process is interrupted and restarts, even if the checkpoint or the starting LSN falls in the middle of a transaction, **the entire transaction is retransmitted**. This behavior guarantees that clients receive complete transactions without missing any intermediate changes, maintaining data integrity across the replication stream​. Logical decoding clients are responsible for avoiding ill effects from handling the same message more than once. Clients may wish to record the last LSN they saw when decoding and skip over any repeated data or (when using the replication protocol) request that decoding start from that LSN rather than letting the server determine the start point.

### PUBLICATION

A publication is a set of changes generated from a table or a group of tables, and might also be described as a change set or replication set. Each publication exists in only one database.

Publications are different from schemas and do not affect how the table is accessed. Each table can be added to multiple publications if needed. Publications may currently only contain tables. Objects must be added explicitly, except when a publication is created for ALL TABLES.

### OUTPUT PLUGIN

Output plugins are the components used to decode WAL changes and transform them into a specific format that can be consumed by replication clients. These plugins are notified about the change events that need to be processed and sent via various callbacks. These callbacks are only invoked when the transaction actually commits.

YugabyteDB supports the following four output plugins:
* `yboutput`
* `pgoutput`
* `test_decoding`
* `wal2json`

All these plugins are pre-packaged with YugabyteDB and do not require any external installation.

### LSN

LSN in YugabyteDB is an unsigned 64-bit integer that uniquely identifies a change record or a transaction boundary record that is consumed from a given replication slot.

In YugabyteDB, LSN values from different slots are considered unrelated and should not be compared. In YugabyteDB, LSN no longer represents the byte offset of a WAL record. 

LSN values for a single replication slot satisfy the following properties: 

- **Uniqueness** 

  - LSN values for the change and transaction boundary records for a given replication slot are unique. In particular, changes from different tablets of the same table will have unique LSN values.

* **Ordering** 

  - LSN values can be compared ( <, >, = ).

  - The LSN of the BEGIN record of a transaction will be strictly lower than the LSN of the changes in that transaction that in turn will be strictly lower than the LSN of the COMMIT record of the same transaction.

  - The LSNs of change records within a transaction will be in increasing order and will correspond to the order in which those changes were made in that transaction. That is, the LSN of an earlier change will have a strictly lower value than the LSN of a later change within the same transaction. This is the case even if the changes correspond to rows in different tablets of the same or different tables.

  - The LSN of the commit record of a transaction will be strictly lower than the LSN of the BEGIN record of  a transaction with greater commit time.

- **Determinism** 

  - For a given replication slot, the LSN value of a change record (or a transaction boundary record) remains the same for the lifetime of that replication slot. In particular, this is true across server and client restarts and client reconnections. Thus, LSN values for a single replication slot may be used to uniquely identify records that are consumed from that replication slot. The values can be compared for determining duplicates at the client side.

### REPLICA IDENTITY

Replica identity is a table level parameter that controls the amount of information being written to the change records. YugabyteDB supports the following four replica identities:

* CHANGE (default)
* DEFAULT
* FULL
* NOTHING

The replica identity `INDEX` is not supported in YugabyteDB. Replica identity `CHANGE` is the best performant and the **default** replica identity. The replica identity of a table can be changed by performing an alter table. However, for a given slot, the alter tables performed to change the replica identity after the creation of the slot will have no effect. This means that the effective replica identity for any table for a slot, is the replica identity of the table that existed at the time of slot creation. A dynamically created table (a table created after slot creation) will have the default replica identity. For a replica identity modified after slot creation to take effect, a new slot will have to be created after performing the Alter table. 

The tserver flag [ysql_yb_default_replica_identity](../../../../reference/configuration/yb-tserver/#ysql_yb_default_replica_identity) determines the default replica identity for user tables at the time of table creation. This flag has a default value `CHANGE`. The purpose of this flag is to set the replica identities for dynamically created tables. In order to create a dynamic table with desired replica identity, the flag must be set accordingly and then the table must be created.

{{< note title="Advisory" >}}

Users should refrain from altering the replica identity of a dynamically created table for at least 5 minutes after its creation.

{{< /note >}}

### REPLICATION PROTOCOLS

Postgres has defined protocols for replication that needs to be followed by clients to establish replication connection as well as message structures for streaming data. This includes two protocols - Streaming Replication protocol ([PG documentation](https://www.postgresql.org/docs/11/protocol-replication.html)), Logical Streaming Replication protocol ([PG documentation](https://www.postgresql.org/docs/11/protocol-logical-replication.html)).

The logical streaming replication protocol sends individual transactions one by one. This means that all messages between a pair of `BEGIN` and `COMMIT` messages belong to the same transaction.

YugabyteDB supports both the streaming replication protocols used in Postgres to support logical replication, maintaining the same semantics described in Postgres. 

* **Streaming Replication Protocol**: This protocol is followed by all output plugins.

* **Logical Streaming Replication Protocol**: This protocol is followed by `pgoutput` and `yboutput`, in addition to the Streaming replication protocol.

{{< note title="Note" >}}

Note: YugabyteDB does not support Physical Replication.

{{< /note >}}

{{< tip title="Explore" >}}
<!--TODO (Sumukh): Fix the Link to the getting started section. -->
See [Getting Started with Logical Replication](../../../explore/logical-replication/getting-started) in Explore to setup Logical Replication in YugabyteDB.

{{< /tip >}}


## **Guarantees**

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

CDC in YugabyteDB being based on the Postgres Logical Replication model means:

1. CDC user persona will be a 'PG Replication client'.

2. A standard replication connection is used for consumption and all the server side configurations of authentication, authorizations, SSL modes and connection load balancing automatically can be leveraged.


## Limitations

1. LSN Comparisons Across Slots

    In the case of YugabyteDB, the LSN  does not represent the byte offset of a WAL record. Hence, arithmetic on LSN and any other usages of the LSN making this assumption will not work. Also, currently, comparison of LSN values from messages coming from different replication slots is not supported.
    
2. The functions listed below are currently unsupported:

   - `pg_current_wal_lsn`
   - `pg_wal_lsn_diff`
   - `IDENTIFY SYSTEM`
   - `txid_current`
   - `pg_stat_replication`
   
   Additionally, the functions responsible for pulling changes instead of the server streaming it are unsupported as well. They are described in the [Replication Functions](https://www.postgresql.org/docs/11/functions-admin.html#FUNCTIONS-REPLICATION) section in the Postgres documentation.
   
3. Restriction on DDLs
    
    DDL operations should not be performed from the time of replication slot creation till the start of snapshot consumption of the last table.
    
4. There should be a primary key on the table you want to stream the changes from.

5. CDC is not supported on a target table for xCluster replication [11829](https://github.com/yugabyte/yugabyte-db/issues/11829).

6. Currently we don't support schema evolution for changes that require table rewrites (ex: ALTER TYPE).

7. YCQL tables aren't currently supported. Issue [11320](https://github.com/yugabyte/yugabyte-db/issues/11320).

8. Support for point-in-time recovery (PITR) is tracked in issue [10938](https://github.com/yugabyte/yugabyte-db/issues/10938).

9. Support for transaction savepoints is tracked in issue [10936](https://github.com/yugabyte/yugabyte-db/issues/10936).

10. Support for enabling CDC on Read Replicas is tracked in issue [11116](https://github.com/yugabyte/yugabyte-db/issues/11116).