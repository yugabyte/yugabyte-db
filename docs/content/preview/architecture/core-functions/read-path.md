---
title: Read I/O path
headerTitle: Read I/O path
linkTitle: Read I/O path
description: Learn how YugabyteDB manages single-row read operations.
menu:
  preview:
    identifier: read-path
    parent: core-functions
    weight: 1188
type: docs
---

The read I/O path can be illustrated by an example of a single key read that involves identifying a tablet leader which then performs a read operation.

## Tablet leader identification

The user-issued read request interacts with the YQL query layer via a port with the appropriate API (either YSQL or YCQL). This user request is translated by the YQL layer into an internal key, allowing the YQL layer to find the tablet and the YB-TServers hosting it. The YQL layer performs this by making an RPC call to the YB-Master. The response is cached for future uses. Next, the YQL layer issues the read to the YB-TServer that hosts the leader tablet peer. The read is handled by the leader of the Raft group of the tablet owning the internal key. The leader of the tablet Raft group which handles the read request reads from its DocDB and returns the result to the user.

As described in [Write I/O path](../write-path/), the YugabyteDB smart client can route the application requests directly to the correct YB-TServer, avoiding any extra network hops or master lookups.

## Read operation performed by tablet leader

{{<tip>}}
The leader of the tablet Raft group is responsible for handling the read requests and returning the result.
{{</tip>}}

Suppose there is a requirement to read the value where the primary key column `K` has a value `k` from table `T1`. The table `T1` has a key column `K` and a value column `V`. The following diagram depicts the read flow:

![Read path](/images/architecture/read_path_io.png)

The default is strongly-consistent read.

The read queries can be quite complex. The YQL query layer has a fully-optimized query engine to handle queries which contain expressions, built-in function calls, and arithmetic operations.
