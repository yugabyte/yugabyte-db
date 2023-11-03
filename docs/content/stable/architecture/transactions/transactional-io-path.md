---
title: Transactional I/O path
headerTitle: Transactional I/O path
linkTitle: Transactional I/O path
description: Learn how YugabyteDB manages the write path of a transaction.
menu:
  stable:
    identifier: architecture-transactional-io-path
    parent: architecture-acid-transactions
    weight: 1156
type: docs
---

For an overview of common concepts used in YugabyteDB's implementation of distributed transactions, see [Distributed transactions](../distributed-txns/). 

The write path of a transaction is used for modifying multiple keys and the read path is used for reading a consistent combination of values from multiple tablets.

## Write path

The write path can be demonstrated through the lifecycle of a single distributed write-only transaction. Suppose it is required to modify rows with keys `k1` and `k2`. If they belong to the same tablet, the transaction can be executed as a [single-shard transaction](../../core-functions/write-path/), in which case atomicity would be ensured by the fact that both updates would be replicated as part of the same Raft log record. However, in the most general case, these keys would belong to different tablets, and that is the working assumption.

The following diagram depicts the high-level steps of a distributed write-only transaction, not including
any conflict resolution:

![distributed_txn_write_path](/images/architecture/txn/distributed_txn_write_path.svg)

### Client requests transaction

The client sends a request to a YugabyteDB tablet server that requires a distributed transaction. The following example uses an extension to CQL:

 ```sql
 START TRANSACTION;
 UPDATE t1 SET v = 'v1' WHERE k = 'k1';
 UPDATE t2 SET v = 'v2' WHERE k = 'k2';
 COMMIT;
 ```

The tablet server that receives the transactional write request becomes responsible for driving all the steps involved in this transaction. This orchestration of transaction steps is performed by a component called a transaction manager. Every transaction is handled by exactly one transaction manager.

### Create a transaction record

A transaction ID is assigned and a transaction status tablet is selected to keep track of a transaction status record that has the following fields:

- Status that can be pending, committed, or aborted.
- Commit hybrid timestamp, if committed.
- List of IDs of participating tablets, if committed.

It makes sense to select a transaction status tablet in a way such that the transaction manager's tablet server is also the leader of its Raft group, because this allows to cut the RPC latency on querying and updating the transaction status. But in the most general case, the transaction status tablet might not be hosted on the same tablet server that initiates the transaction.

### Write provisional records

Provisional records are written to tablets containing the rows that need to be modified. These provisional records contain the transaction ID, the values that need to be written, and the provisional hybrid timestamp which is not the final commit timestamp and will in general be different for different provisional records within the same transaction. In contrast, there is only one commit hybrid timestamp for the entire transaction.

As the provisional records are written, it is possible to encounter conflicts with other transactions. In
this case, the transaction would have to be aborted and restarted. These restarts still happen transparently to the client up to a certain number of retries.

### Commit the transaction

When the transaction manager has written all the provisional records, it commits the transaction by sending an RPC request to the transaction status tablet. The commit operation can only succeed if the transaction has not yet been aborted due to conflicts. The atomicity and durability of the commit operation is guaranteed by the transaction status tablet's Raft group. Once the commit operation is complete, all provisional records immediately become visible to clients.

The commit request the transaction manager sends to the status tablet includes the list of tablet IDs of all tablets that participate in the transaction. No new tablets can be added to this set by this point. The status tablet needs this information to orchestrate cleaning up provisional records in participating tablets.

### Send the response back to client

The YQL engine sends the response back to the client. If any client (either the same one or different) sends a read request for the keys that were written, the new values are guaranteed to be reflected in the response, because the transaction is already committed. This property of a database is sometimes called the "read your own writes" guarantee.

### Asynchronously apply and clean up provisional records

This step is coordinated by the transaction status tablet after it receives the commit message for our transaction and successfully replicates a change to the transaction's status in its Raft group. The transaction status tablet already knows what tablets are participating in this transaction, so it sends cleanup requests to them. Each participating tablet records a special "apply" record into its Raft log, containing the transaction ID and commit timestamp. When this record is Raft-replicated in the participating tablet, the tablet removes the provisional records belonging to the transaction, and writes regular records with the correct commit timestamp to its RocksDB databases. These records are virtually indistinguishable from those written by regular single-row operations.

Once all participating tablets have successfully processed these apply requests, the status tablet can delete the transaction status record because all replicas of participating tablets that have not yet cleaned up provisional records (for example, slow followers) will do so based on information available locally within those tablets. The deletion of the status record happens by writing a special "applied everywhere" entry to the Raft log of the status tablet. Raft log entries belonging to this transaction will be cleaned up from the status tablet's Raft log as part of regular garbage-collection of old Raft logs soon after this point.

## Read path

YugabyteDB is a multiversion concurrency control (MVCC) database, which means it internally keeps track of multiple versions of the same value. Read operations do not take any locks. Instead, they rely on the MVCC timestamp in order to read a consistent snapshot of the data. A long-running read operation, either single-shard or cross-shard, can proceed concurrently with write operations modifying the same key.

As described in [Single-row transactions](../single-row-transactions/), up-to-date reads are performed from a single tablet (shard), with the most recent value of a key being the value written by the last committed Raft log record known to the Raft leader. For reading multiple keys from different tablets, though, it must be ensured that the values read come from a recent consistent snapshot of the database. The following clarifies these properties of the selected snapshot:

- Consistent snapshot: The snapshot must show any transaction's records fully, or not show them at all. It cannot contain half of the values written by a transaction and omit the other half. The snapshot consistency is ensured by performing all reads at a particular hybrid time (`ht_read`), and ignoring any records with later hybrid time.

- Recent snapshot: The snapshot includes any value that any client might have already seen, which means all values that were written or read before this read operation was initiated. This also includes all previously written values that other components of the client application might have written to or read from the database. The client performing the current read might rely on the presence of those values in the result set because those other components of the client application might have communicated this data to the current client through asynchronous communication channels. To ensure the snapshot is recent, the read operation needs to be restarted when it is determined that the chosen hybrid time was too early, that is, there are some records that could have been written before the read operation was initiated but have a hybrid time later than the currently set `ht_read`.

The following diagram depicts the process:

![Distributed transaction read path diagram](/images/architecture/txn/distributed_txn_read_path.svg)

### Handle the client's request and initialize read transaction

The client's request to either the YCQL, YEDIS, or YSQL API arrives at the YQL engine of a tablet server. The YQL engine detects that the query requests rows from multiple tablets and starts a read-only transaction. A hybrid time `ht_read` is selected for the request, which could be either the current hybrid time on the YQL engine's tablet server or the [safe time](../single-row-transactions/#safe-timestamp-assignment-for-a-read-request) on one of the involved tablets. The latter case would reduce waiting for safe time for at least that tablet and is therefore better for performance. Typically, due to YugabyteDB load-balancing policy, the YQL engine receiving the request also hosts some of the tablets that the request is reading, allowing to implement the more performant second option without an additional RPC round-trip.

In addition, a point in time called `global_limit` is selected, computed as `physical_time + max_clock_skew`, which helps determine if a particular record was definitely written after the read request had started. `max_clock_skew` is a globally-configured bound on clock skew between different YugabyteDB servers.

### Read from all tablets at the specific hybrid time

The YQL engine sends requests to all tablets from which the transaction needs to read. Each tablet waits for `ht_read` to become a safe time to read at, according to the [definition of safe time](../single-row-transactions/#definition-of-safe-time), and then starts executing its part of the read request from its local DocDB.

When a tablet server sees a relevant record with a hybrid time `ht_record`, it executes the following logic:

- If `ht_record` &le; `ht_read`, include the record in the result.
- If `ht_record` > `definitely_future_ht`, exclude the record from the result. `definitely_future_ht` means a hybrid time such that a record with a later hybrid time than that was definitely written after the read request had started. For now, `definitely_future_ht` can be assumed to be `global_limit`. 
- If `ht_read` < `ht_record` &le; `definitely_future_ht`, it is not known if this record was written before or after the start of the read request. But it cannot be omitted from the result because if it was in fact written before the read request, this may produce a client-observed inconsistency. Therefore, the entire read operation must be restarted with an updated value of `ht_read` = `ht_record`.

To prevent an infinite loop of these read restarts, a tablet-dependent hybrid time value `local_limit`<sub>`tablet`</sub> is returned to the YQL engine, computed as the current safe time in this tablet. It is now known that any record (regular or provisional) written to this tablet with a hybrid time later than `local_limit`<sub>`tablet`</sub> could not have possibly been written before the start of the read request. Therefore, the read transaction would not have to be restarted if a record with a hybrid time later than `local_limit`<sub>`tablet`</sub> is observed in a later attempt to read from this tablet within the same transaction, and `definitely_future_ht` = `min(global_limit, local_limit`<sub>`tablet`</sub>`)` is set on future attempts.

### Tablets query the transaction status

As each participating tablet reads from its local DocDB, it might encounter provisional records for which it does not yet know the final transaction status and commit time. In these cases, it would send a transaction status request to the transaction status tablet. If a transaction is committed, it is treated as if DocDB already contained permanent records with hybrid time equal to the transaction's commit time. The [cleanup](#asynchronously-apply-and-clean-up-provisional-records) of provisional records happens independently and asynchronously.

### Tablets respond to YQL

Each tablet's response to YQL contains the following information:

- Whether or not read restart is required.
- `local_limit`<sub>`tablet`</sub> to be used to restrict future read restarts caused by this tablet.
- The actual values that have been read from this tablet.

### YQL sends the response to the client

As soon as all read operations from all participating tablets succeed and it has been determined that there is no need to restart the read transaction, a response is sent to the client using the appropriate wire protocol.
