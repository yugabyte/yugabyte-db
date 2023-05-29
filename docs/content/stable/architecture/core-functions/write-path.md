---
title: Write I/O path
headerTitle: Write I/O path
linkTitle: Write I/O path
description: Learn how YugabyteDB manages single-row write operations.
menu:
  stable:
    identifier: write-path
    parent: core-functions
    weight: 1186
type: docs
---

The write I/O path can be illustrated by an example of a single key write that involves the write operations being processed by the YQL layer and prepared for replication by the tablet leader.

For information on more complex cases, such as distributed transactions with multiple keys requiring atomical updates, see [Distributed transactions](../../transactions/distributed-txns/).

## Write operation processing by YQL layer

The user-issued write request interacts with the YQL query layer via a port with the appropriate API (either YSQL or YCQL). This user request is translated by the YQL layer into an internal key. As described in [Sharding](../../docdb-sharding/sharding/), each key is owned by one tablet. To determine which tablet owns a given key, the YQL layer makes an RPC call to the YB-Master. The response is cached for future uses.

YugabyteDB has a [smart client](../../../drivers-orms/smart-drivers/) that can cache the location of the tablet directly and can therefore save the extra network hop, therefore allowing it to send the request directly to the YQL layer of the appropriate YB-TServer hosting the tablet leader. If the YQL layer finds that the tablet leader is hosted on the local node, the RPC call becomes a local function call and saves the time needed to serialize and deserialize the request, and then send it over the network.

The YQL layer then issues the write to the YB-TServer hosting the tablet leader. The write is handled by the leader of the Raft group of the tablet owning the key.

## Preparation of the operation for replication by tablet leader

The following diagram shows the tablet leader's process to prepare the operation for replication:

![single_shard_txns_insert_if_not_exists](/images/architecture/txn/single_shard_txns_insert_if_not_exists.svg)

The leader of the tablet's Raft group performs the following sequence:

* Verifies that the operation being performed is compatible with the current schema.
* Takes a lock on the key using a local in-memory lock manager. Note that this locking mechanism does not exist on followers.
* Reads data if necessary (for read-modify-write or conditional update operations).
* Prepares the batch of changes to be written to DocDB. This write batch is very close to the final set of RocksDB key-value pairs to be written, only lacking the final hybrid timestamp at the end of each key.

## Raft replication of the write operation

The sequence of the Raft replication of the write operation can be described as follows:

* The leader appends the batch to its Raft log and chooses a hybrid timestamp for the write operation.
* Replicates the data using Raft to its peers.
* Upon successful Raft replication, applies the data into its local DocDB.
* Responds with success to the user.

The follower tablets receive the data replicated using Raft and apply it into their local DocDB once it is known to have been committed. The leader piggybacks the advancement of the commit point in subsequent RPC requests, as follows:

* The Raft entry containing the write batch is replicated to the majority of the tablet's Raft group peers.
* After receiving a "replication successful" callback from the Raft subsystem, the leader applies the write batch to its local RocksDB.
* The next update from the leader notifies followers that the entry has been committed, and the followers apply the write batch to their RocksDB instances.

## Response to the client

Information pending.

## Examples

Suppose there is a requirement to insert values `k` and `v` into a table `T1` that had a key column `K` and a value column `V`. The following diagram depicts the write flow:

![write_path_io](/images/architecture/write_path_io.png)

Note that the preceding case has been simplified by assuming that the user application sends the write query to a random YugabyteDB server, which then routes the request appropriately.

Specifically for YCQL, using the YugabyteDB smart client would allow you to avoid the extra network hop.
