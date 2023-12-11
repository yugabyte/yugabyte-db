---
title: Read Restart error
headerTitle: Read Restart error
linkTitle: Read Restart error
description: Details about the Read Restart error
menu:
  preview:
    identifier: architecture-read-restart-error
    parent: architecture-acid-transactions
    weight: 50
type: docs
rightNav:
  hideH4: true
---

The distributed nature of YugabyteDB means that clock skew can be present between the different physical nodes in the database cluster. And given that YugabyteDB is a multi-version concurrency control (MVCC) database, this clock skew can sometimes result in an unresolvable ambiguity of whether a version of data should be/ or not be part of a read in snapshot-based transaction isolations (i.e., repeatable read & read committed). There are multiple solutions for this problem, [each with their own challenges](https://www.yugabyte.com/blog/evolving-clock-sync-for-distributed-databases/). PostgreSQL doesn't require defining semantics around read restart errors because it is a single-node database without clock skew.

YugabyteDB doesn't require atomic clocks, but instead allows a configurable setting for maximum clock skew. Plus, there are optimizations in YugabyteDB to resolve this ambiguity internally with best-effort. However, when it can't be resolved internally, YugabteDB will throw a `read restart` error to the external client, something like this:

```
ERROR:  Query error: Restart read required at: { read: { physical: 1656351408684482 } local_limit: { physical: 1656351408684482 } global_limit: <min> in_txn_limit: <max> serial_no: 0 }
```

A detailed scenario that explains how clock skew can result in the above mentioned ambiguity around data visibility:

* A client starts a distributed transaction by connecting to YSQL on a node `N1` in the YugabyteDB cluster and issues a statement which reads data from multiple shards on different physical YB-TServers in the cluster. For this issued statement, the read point that defines the snapshot of the database at which the data will be read, is picked on a YB-TServer node `M` based on the current time of that YB-TServer. Depending on the scenario, node `M` may or may not be the same as `N1`, but that is not relevant to this discussion. Consider `T1` to be the chosen read time.
* The node `N1` might collect data from many shards on different physical YB-TServers. In this pursuit, it will issue requests to many other nodes to read data.
* Assuming that node `N1` reads from node `N2`, it is possible that data had already been written on node `N2` with a write timestamp `T2` (> `T1`) but it had been written before the read was issued. This could be caused by clock skew if the physical clock on node `N2` ran ahead of node `M`, resulting in the write done in the past still having a write timestamp later than `T1`.

  Note that the clock skew between all nodes in the cluster is always in a [max_clock_skew_usec](../../../reference/configuration/yb-tserver/#max-clock-skew-usec) bound due to clock synchronization algorithms.
* For writes with a write timestamp later than `T1` + `max_clock_skew`, the database can be sure that these writes were done after the read timestamp had been chosen. But for writes with a write timestamp between `T1` and `T1` + `max_clock_skew`, node `N2` can find itself in an ambiguous situation, such as the following:

  * It should return the data even if the client issued the read from a different node after writing the data, because the following guarantee needs to be maintained: the database should always return data that was committed in the past (past refers to the user-perceived past, and not based on machine clocks).

  * It should not return the data if the write was performed in the future (that is, after the read point had been chosen) **and** had a write timestamp later than the read point. Because if that was allowed, everything written in future would be trivially visible to the read. Note that the latter condition is important because it is okay to return data that was written after the read point was picked if the write timestamp was earlier than the read point (this doesn't break and consistency or isolation guarantees).

* If node `N2` finds writes in the range `(T1, T1+max_clock_skew]`, to avoid breaking the strong guarantee that a reader should always be able to read what was committed earlier and to avoid reading data with a later write timestamp which was also actually written after the read had been issued, node `N2` raises a `Read restart` error.
