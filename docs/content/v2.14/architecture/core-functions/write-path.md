---
title: Write IO path
headerTitle: Write IO path
linkTitle: Write IO path
description: Learn how YugabyteDB manages single-row write operations.
menu:
  v2.14:
    identifier: write-path
    parent: core-functions
    weight: 1186
type: docs
---

For purposes of simplicity, let us take the case of a single key write. The case of distributed
transactions where multiple keys need to be updated atomically is covered in the
[distributed transactions IO path](../../transactions/distributed-txns/) section,
and heavily relies on the single-tablet write path described here.

## Step 1. YQL layer processes the write operation

The user-issued write request first hits the YQL query layer on a port with the appropriate API, which is either YSQL or YCQL. This user request is translated by the YQL layer into an internal key.
Recall from the [sharding](../../concepts/sharding/) section that each key is owned
by exactly one tablet. This tablet as well as the YB-TServers hosting it can easily be determined by
making an RPC call to the YB-Master. The YQL layer makes this RPC call to determine the
tablet/YB-TServer owning the key and caches the result for future use.

YugabyteDB has a [smart client](../../../develop/client-drivers/java/) that can cache the location of the
tablet directly and can therefore save the extra network hop. This allows it to send the request
directly to the YQL layer of the appropriate YB-TServer which hosts the tablet leader. If the YQL
layer finds that the tablet leader is hosted on the local node, the RPC call becomes a local
function call and saves the time needed to serialize and deserialize the request and send it over
the network.

The YQL layer then issues the write to the YB-TServer that hosts the tablet leader. The write is
handled by the leader of the Raft group of the tablet owning the key.

## Step 2. Tablet leader prepares the operation for replication

![single_shard_txns_insert_if_not_exists](/images/architecture/txn/single_shard_txns_insert_if_not_exists.svg)

The leader of the tablet's Raft group performs the following sequence:

* Verifies that the operation being performed is compatible with the current schema.
* Takes a lock on the key using a local in-memory lock manager. Note that this locking mechanism
  does not exist on followers.
* Reads data if necessary (for read-modify-write or conditional update operations).
* Prepares the batch of changes to be written to DocDB. This *write batch* is very close to the
  final set of RocksDB key-value pairs to be written, only lacking the final hybrid timestamp
  at the end of each key.

## Step 3. Raft replication of the write operation

* The leader appends the batch to its Raft log and chooses a *hybrid timestamp*, to be used as the
  timestamp of the write operation.
* Replicates the data using Raft to its peers.
* upon successful Raft replication, applies the data into its local DocDB
* responds with success to the user

The follower tablets receive the data replicated using Raft and apply it into their local DocDB once
it is known to have been committed. The leader piggybacks the advancement of the commit point in subsequent RPC requests.

* The Raft entry containing the write batch is replicated to the majority of the tablet's Raft group peers.
* After receiving a "replication successful" callback from the Raft subsystem, the leader applies  the write batch to its local RocksDB
* In the next update that the leader sends to followers, it also notifies followers that our entry has been committed, and the followers apply the write batch to their RocksDB instances.

## Step 4. Sending response to the client

## An example

As an example, let us assume the user wants to insert into a table T1 that had a key column K and a value column V the values (k, v). The write flow is depicted below.

![write_path_io](/images/architecture/write_path_io.png)

Note that the above scenario has been greatly simplified by assuming that the user application sends the write query to a random YugabyteDB server, which then routes the request appropriately. Specifically for YCQL, the use of the YugabyteDB smart client is recommended for removing the extra network hop.
