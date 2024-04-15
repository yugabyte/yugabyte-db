---
title: Distributed transactions
headerTitle: Distributed transactions
linkTitle: Distributed transactions
description: Distributed ACID transactions modify multiple rows spread across multiple shards.
menu:
  preview:
    identifier: architecture-distributed-acid-transactions
    parent: architecture-acid-transactions
    weight: 70
aliases:
  - /architecture/concepts/transactions/
type: docs
---

YugabyteDB supports distributed transactions based on principles of atomicity, consistency, isolation, durability (ACID) that modify multiple rows in more than one shard. This enables strongly consistent secondary indexes, as well as multi-table and multi-row ACID operations in both YCQL and YSQL contexts.

After you are familiar with the preceding concepts, refer to [Transactional I/O path](../transactional-io-path/) for an overview of a distributed transaction's lifecycle.

## Provisional records

Just as YugabyteDB stores values written by single-shard ACID transactions into [DocDB](../../docdb/persistence/), it needs to store uncommitted values written by distributed transactions in a similar persistent data structure. However, they cannot be written to DocDB as regular values, because they would then become visible at different times to clients reading through different tablet servers, allowing a client to see a partially applied transaction and thus breaking atomicity. YugabyteDB therefore writes provisional records to all tablets responsible for the keys the transaction is trying to modify. These transactions are called provisional as opposed to regular (permanent) records because they are invisible to readers until the transaction commits.

Provisional records are stored in a separate RocksDB instance in the same tablet peer (referred to as IntentsDB, as opposed to RegularDB, for regular records). Compared to other possible design options, such as storing provisional records inline with the regular records, or putting them in the same RocksDB instance together with regular records, the chosen approach has the following benefits:

- Scanning all provisional records is straightforward, which is helpful in cleaning up aborted or abandoned transactions.
- During the read path, there is a need to handle provisional records very differently from the regular records, and putting them in a separate section of the RocksDB key space allows to simplify the read path.
- Storing provisional records in a separate RocksDB instance allows each one to have different storage, compaction, and flush strategies.

### Encoding details of provisional records

There are three types of RocksDB key-value pairs corresponding to provisional records, omitting the one-byte prefix that puts these records before all regular records in RocksDB, as per the following diagram:

![DocDB storage, including provisional records](/images/architecture/txn/provisional_record_storage.svg)

#### Primary provisional records

```output
DocumentKey, SubKey1, ..., SubKeyN, LockType, ProvisionalRecordHybridTime -> TxnId, Value
```

The `DocumentKey`, `SubKey1`, ..., `SubKey` components exactly match those in DocDB's [encoding](../../docdb/data-model) of paths to a particular subdocument (for example, a row, a column, or an element in a collection-type column) to RocksDB keys.

Each of these primary provisional records also acts as a persistent revocable lock. There are some similarities as well as differences when compared to [blocking in-memory locks](../isolation-levels/) maintained by every tablet's lock manager. These persistent locks can be of any of the same types as for in-memory leader-only locks (SI write, serializable write and read, and a separate strong and weak classification for handling nested document changes). However, unlike the leader-side in-memory locks, the locks represented by provisional records can be revoked by another conflicting transaction. The conflict resolution subsystem makes sure that for any two conflicting transactions, at least one of them is aborted.

As an example, suppose a snapshot isolation transaction is setting column `col1` in row `row1` to `value1`. Then `DocumentKey` is `row1` and `SubKey1` is `col1`. Suppose the provisional record was written into the tablet with a hybrid timestamp `1516847525206000`, and the transaction ID is `7c98406e-2373-499d-88c2-25d72a4a178c`. There will be the following provisional record values in RocksDB:

  ```output
  row1, WeakSIWrite, 1516847525206000 -> 7c98406e-2373-499d-88c2-25d72a4a178c
  row1, col1, StrongSIWrite, 1516847525206000 -> 7c98406e-2373-499d-88c2-25d72a4a178c, value1
  ```

The `WeakSIWrite` lock type is used for the row (the parent of the column being written), and `StrongSIWrite` is used for the column itself. The provisional record for the column is also where the column's value being written by the transaction is stored.

#### Transaction metadata records

```output
TxnId -> StatusTabletId, IsolationLevel, Priority
```

- `StatusTabletId` is the ID of the tablet that keeps track of this transaction's status. Unlike the case of tables and tablets holding user data, where a [hash-based mapping](../../concepts/sharding/) is used from keys to tablets, there is no deterministic way to compute the transaction status tablet ID by transaction ID, so this information must be explicitly passed to all components handling a particular transaction.
- `Isolation Level` [snapshot isolation](https://en.wikipedia.org/wiki/Snapshot_isolation) or [serializable isolation](https://en.wikipedia.org/wiki/Serializability).
- `Priority` This priority is assigned randomly during transaction creation, when the [Fail-on-Conflict](../concurrency-control/#fail-on-conflict) concurrency control policy is used.

#### Provisional record keys indexed by transaction ID

```output
TxnId, HybridTime -> primary provisional record key
```

This mapping enables finding all provisional RocksDB records belonging to a particular transaction. This is used when cleaning up committed or aborted transactions. Note that because multiple RocksDB key-value pairs belonging to primary provisional records can be written for the same transaction with the same hybrid timestamp, an increasing counter (called write ID) is used at the end of the encoded representation of hybrid time in order to obtain unique RocksDB keys for this reverse index. This write ID is shown as `.0`, `.1`, and so on, in `T130.0`, `T130.1` shown in the diagram in [Encoding details of provisional records](#encoding-details-of-provisional-records).

## Transaction status tracking

Atomicity means that either all values written by a transaction are visible or none are visible. YugabyteDB already provides atomicity of single-shard updates by replicating them via Raft and applying them as one write batch to the underlying DocDB storage engine. The same approach could be reused to make transaction status changes atomic. The status of transactions is tracked in a so-called transaction status table. This table, under the covers, is just another sharded table in the system, although it does not use RocksDB and instead stores all its data in memory, backed by the Raft WAL. The transaction ID (a globally unique ID) serves as the key in the table, and updates to a transaction's status are basic single-shard ACID operations. By setting the status to `committed` in that transaction's status record in the table, all values written as part of that transaction become atomically visible.

A transaction status record contains the following fields for a particular transaction ID:

- Status: pending, committed, or aborted. All transactions start in the pending status and then progress to committed or aborted, in which they remain permanently until cleaned up.

After a transaction is committed, the following two fields are set:

- Commit hybrid timestamp. This timestamp is chosen as the current hybrid time at the transaction status tablet at the moment of appending the transaction committed entry to its Raft log. It is then used as the final MVCC timestamp for regular records that replace the transaction's provisional records when provisional records are being applied and cleaned up.
- List of IDs of participating tablets. After a transaction commits, the final set of tablets to which the transaction has written is known. The tablet server managing the transaction sends this list to the transaction status tablet as part of the commit message, and the transaction status tablet makes sure that all participating tablets are notified of the transaction's committed status. This process might take multiple retries, and the transaction record can only be cleaned up after this is done.

## Impact of failures

Provisional records are written to all the replicas of the tablets responsible for the keys being modified in a transaction. When a node with the tablet that has received or is about to receive the provisional records fails, a new leader is elected for the tablet in a few seconds(`~2s`) as described in [Leader Failure](../../core-functions/high-availability/#tablet-peer-leader-failure). The query layer waits for leader election to occur and then the transaction proceeds further with the newly elected leader. In this case, the time taken for the transaction to complete increases by the time taken for the leader election.

The transaction manager (typically, the node the client is connected to) sends heartbeats to the transaction status tablet that maintains information about the transaction. When the manager fails, these heartbeats stop and the provisional records expire after certain time. At this point, the status tablet automatically cancels this transaction, so the related provisional records would no longer block conflicting transactions waiting on the same keys. Clients connected to the failed manager receive an error message similar to the following:

```output.sql
FATAL:  57P01: terminating connection due to unexpected postmaster exit
FATAL:  XX000: Network error: recvmsg error: Connection refused
```

As the client is unaware of the transaction ID because the client to transaction ID mapping cannot be regenerated, it would be the responsibility of the client to restart the transaction. Other clients with transactions that were blocked on the provisional records written by the failed manager will have to wait for the transaction to be expired due to heartbeat timeout and then proceed normally.
