---
title: Read Restart error
headerTitle: Read Restart error
linkTitle: Read Restart error
description: Learn about the Read Restart error which stem due to the data distribution across more than one node.
menu:
  preview:
    identifier: architecture-read-restart-error
    parent: architecture-acid-transactions
    weight: 900
type: docs
rightNav:
  hideH4: true
---

The distributed nature of YugabyteDB means that clock skew can be present between different physical nodes in the database cluster. Given that YugabyteDB is a multi-version concurrency control (MVCC) database, this clock skew can sometimes result in an unresolvable ambiguity of whether a version of data should, or not be part of a read in snapshot-based transaction isolations (that is, repeatable read and read committed). There are multiple solutions for this problem, [each with their own challenges](https://www.yugabyte.com/blog/evolving-clock-sync-for-distributed-databases/). PostgreSQL doesn't require defining semantics around read restart errors because it is a single-node database without clock skew.

YugabyteDB doesn't require atomic clocks, but instead allows a configurable setting for maximum clock skew. Additionally, there are optimizations in YugabyteDB to resolve this ambiguity internally with best-effort. However, when it can't be resolved internally, YugabyteDB will output a `read restart` error to the external client, similar to the following:

```output
ERROR:  Query error: Restart read required at: { read: { physical: 1656351408684482 } local_limit: { physical: 1656351408684482 } global_limit: <min> in_txn_limit: <max> serial_no: 0 }
```

A detailed scenario that explains how clock skew can result in the above mentioned ambiguity around data visibility is as follows:

* A client starts a distributed transaction by connecting to YSQL on a node `N1` in the YugabyteDB cluster, and issues a statement which reads data from multiple shards on different physical YB-TServers in the cluster. For this issued statement, the read point that defines the snapshot of the database at which the data will be read, is picked on a YB-TServer node `M` based on the current time of that YB-TServer. Depending on the scenario, node `M` may or may not be the same as `N1`, but that is not relevant to this discussion. Consider `T1` to be the chosen read time.
* The node `N1` might collect data from many shards on different physical YB-TServers. In this pursuit, it will issue requests to many other nodes to read data.
* Assuming that node `N1` reads from node `N2`, it is possible that data had already been written on node `N2` with a write timestamp `T2` (> `T1`) but it had been written before the read was issued. This could be caused by clock skew if the physical clock on node `N2` ran ahead of node `M`, resulting in the write done in the past still having a write timestamp later than `T1`.

  Note that the clock skew between all nodes in the cluster is always in a [max_clock_skew_usec](../../../reference/configuration/yb-tserver/#max-clock-skew-usec) bound due to clock synchronization algorithms.
* For writes with a write timestamp later than `T1` + `max_clock_skew`, the database can be sure that these writes were done after the read timestamp had been chosen. But for writes with a write timestamp between `T1` and `T1` + `max_clock_skew`, node `N2` can find itself in an ambiguous situation, such as the following:

  * It should return the data even if the client issued the read from a different node after writing the data, because the following guarantee needs to be maintained: the database should always return data that was committed in the past (past refers to the user-perceived past, and not based on machine clocks).

  * It should not return the data if the write is performed in the future (that is, after the read point had been chosen) **and** had a write timestamp later than the read point. Because if that is allowed, everything written in future would be trivially visible to the read. Note that the latter condition is important because it is okay to return data that was written after the read point was picked, if the write timestamp was earlier than the read point (this doesn't break any consistency or isolation guarantees).

* If node `N2` finds writes in the range `(T1, T1+max_clock_skew]`, to avoid breaking the strong guarantee that a reader should always be able to read what was committed earlier, and to avoid reading data with a later write timestamp which was also actually written after the read had been issued, node `N2` raises a `Read restart` error.

## Troubleshooting

You can handle and mitigate read restart errors using the following techniques:

- Implement retry logic in the application. Application retries can help mitigate read restart errors. Moreover, a statement or a transaction may fail in other ways such as transaction conflicts or infrastructure failures. Therefore, a retry mechanism is strongly recommended for a cloud-native, distributed database such as YugabyteDB.
- Use SERIALIZABLE READ ONLY DEFERRABLE mode whenever possible. Read restart errors usually occur when the query is a SELECT statement with a large output footprint and there are concurrent writes that satisfy the SELECT statement.

  Using DEFERRABLE will avoid a read restart error altogether. However, the tradeoff is that the statement waits out the maximum permissible clock skew before reading the data (which is max_clock_skew_usec that has a default of 500ms). This is not an issue for large SELECT statements running in the background because latency is not a priority.

  Examples:

  Set transaction properties at the session level.
  ```sql
  SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL SERIALIZABLE READ ONLY DEFERRABLE;
  SELECT * FROM large_table;
  ```

  Enclose the offending query within a transaction block.
  ```sql
  BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE READ ONLY DEFERRABLE;
  SELECT * FROM large_table;
  COMMIT;
  ```
- Using read only, deferrable transactions is not always feasible, either because the query is not read only, or the query is part of a read-write transaction, or because an additional 500ms of latency is not acceptable. In these cases, try increasing the value of `ysql_output_buffer_size`.

  This will enable YugabyteDB to retry the query internally on behalf of the user. As long as the output of a statement hasn't crossed ysql_output_buffer_size to result in flushing partial data to the external client, the YSQL query layer retries read restart errors for all statements in a Read Committed transaction block, for the first statement in a Repeatable Read transaction block, and for any standalone statement outside a transaction block. As a tradeoff, increasing the buffer size also increases the memory consumed by the YSQL backend processes, resulting in a higher risk of out-of-memory errors.

  Be aware that increasing `ysql_output_buffer_size` is not a silver bullet. For example, the COPY command can still raise a read restart error even though the command has a one line output. Increasing `ysql_output_buffer_size` is not useful in this scenario. The application must retry the COPY command instead. Another example is DMLs such as INSERT/UPDATE/DELETE. These do not have enough output to overflow the buffer size. However, when these statements are executed in the middle of a REPEATABLE READ transaction (e.g. BEGIN ISOLATION LEVEL REPEATABLE READ; ... INSERT ... COMMIT;), a read restart error cannot be retried internally by YugabyteDB. The onus is on the application to ROLLBACK and retry the transaction.
