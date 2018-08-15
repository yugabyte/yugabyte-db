---
title: Read IO Path
linkTitle: Read IO Path
description: Read IO Path (Single Shard)
menu:
  1.1-beta:
    identifier: read-path
    parent: core-functions
    weight: 1030
aliases:
  - /architecture/core-functions/read-path/
---

Let us take the case of a single key read.

## Step 1. Identify Tablet Leader

The user-issued read request first hits the YQL query layer on a port with the appropriate protocol
(Cassandra, Redis, PostgreSQL(beta), etc). This user request is translated by the YQL layer into an internal key. The
YQL layer then finds this tablet as well as the YB-TServers hosting it by making an RPC call to the
YB-Master, and caches the response for future. The YQL layer then issues the read to the YB-TServer
that hosts the leader tablet-peer. The read is handled by the leader of the RAFT group of the tablet
owning the internal key. The leader of the tablet RAFT group which handles the read request performs
the read from its DocDB and returns the result to the user.

As mentioned before in the [write IO path
section](../write-path/#step-1-identify-tablet-leader), the YugaByte smart
client can route the application requests directly to the correct YB-TServer avoiding any extra
network hops or master lookups.

## Step 2. Tablet Leader Performs the Read Operation (Default Strongly Consistent Read)

Continuing our previous example, let us assume the user wants to read the value where the primary
key column K has a value k from table T1. From the previous example, the table T1 has a key column K
and a value column V. The read flow is depicted below.

![read_path_io](/images/architecture/read_path_io.png)

Note that the read queries can be quite complex - even though the example here talks about a simple
key-value like table lookup. The YQL query layer has a fully optimized query engine to handle
queries which contain expressions, built-in function calls, arithmetic operations in cases where
valid, etc.
