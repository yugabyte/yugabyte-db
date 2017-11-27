---
date: 2016-03-09T20:08:11+01:00
title: Write IO Path
weight: 102
---

For purposes of simplicity, let us take the case of a single key write. The case of distributed
transactions where multiple keys need to be updated atomically is covered in a separate section. 

## Step 1. Identify Tablet Leader
The user-issued write request first hits the YQL query layer on a port with the appropriate protocol (Cassandra, Redis, etc). This user request is translated by the YQL layer into an internal key. Recall from the section on sharding that given a key, it is owned by exactly one tablet. This tablet as well as the YB-TServers hosting it can easily be determined by making an RPC call to the YB-Master. The YQL layer makes this RPC call to determine the tablet/YB-TServer owning the key and caches the result for future use. 

YugaByte has a **smart client** that can cache the location of the tablet directly and can therefore save the extra network hop. This allows it to send the request directly to the YQL layer of the appropriate YB-TServer which hosts the tablet leader. If the YQL layer finds that the tablet leader is hosted on the local node, the RPC call becomes a library call and saves the work needed to serialize and deserialize the request.

## Step 2. Tablet Leader Performs the Write Operation
The YQL layer then issues the write to the YB-TServer that hosts the tablet leader. The write is handled by the leader of the RAFT group of the tablet owning the internal key. The leader of the tablet RAFT group does operations such as:

* verifying that the operation being performed is compatible with the current schema
* takes a lock on the key using an id-locker
* reads data if necessary (for read-modify-write or conditional update operations)
* replicates the data using RAFT to its peers
* upon successful RAFT replication, applies the data into its local DocDB
* responds with success to the user

The follower tablets receive the data replicated using RAFT and apply it into their local DocDB once it is known to have been committed. The leader piggybacks the advancement of the commit point in subsequent RPCs.

## An Example

As an example, let us assume the user wants to insert into a table T1 that had a key column K and a
value column V the values (k, v). The write flow is depicted below.

![write_path_io](/images/write_path_io.png)

Note that the above scenario has been greatly simplified by assuming that the user application sends the write query to a random YugaByte server, which then routes the request appropriately. In practice, the use of the YugaByte smart client is recommended for removing the extra network hop.
