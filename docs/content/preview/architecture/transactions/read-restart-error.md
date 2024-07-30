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

Read restart errors are raised to maintain the _read-after-commit-visibility_ guarantee: any read query should see all data that was committed before the read query was issued (even in the presence of clock skew between nodes). In other words, read restart errors prevent the following stale read anomaly:
1. First, user X commits some data, for which the database picks a commit timestamp, say commit_time.
2. Next, user X informs user Y about the commit via a channel outside the database, say a phone call.
3. Then, user Y issues a read that picks a read time, which is less than the prior commit_time due to clock skew.
4. As a consequence, without a read restart error, user Y gets an output without the data that user Y was informed about.

YugabyteDB doesn't require atomic clocks, but instead allows a configurable setting for maximum clock skew. Time synchronization protocols such as NTP synchronize commodity hardware clocks periodically to keep the skew low and bounded. Additionally, YugabyteDB has optimizations to resolve this ambiguity internally with best-effort. However, when it can't resolve the error internally, YugabyteDB outputs a `read restart` error to the external client, similar to the following:

```output
ERROR:  Query error: Restart read required at: { read: { physical: 1656351408684482 } local_limit: { physical: 1656351408684482 } global_limit: <min> in_txn_limit: <max> serial_no: 0 }
```

The following scenario describes how clock skew can result in the above mentioned ambiguity around data visibility in detail:

* Tokens 17, 29 are inserted into an empty tokens table. Then, all the tokens from the table are retrieved.

  The SQL commands for the scenario are as follows:
  ```sql
  INSERT INTO tokens VALUES (17);
  INSERT INTO tokens VALUES (29);
  SELECT * FROM tokens;
  ```
* The SELECT must return both 17 and 29.
* However, due to clock skew, the INSERT operation picks a commit time higher than the reference time, while the SELECT picks a lower read time and thus omits the prior INSERT from the result set.

The following diagram shows the order of operations that describe this scenario in detail:

  ![Read Restart Error](/images/architecture/txn/read_restart_error.png)

  The cluster has three tablet servers, namely, `TSERVER 1`, `TSERVER 2`, and `TSERVER 3`. The data for the tokens table is hosted on TSERVER 2 and TSERVER 3. The query layer on TSERVER 1 is serving the SQL requests.

  Moreover, TSERVER 3's clock is running 5 units of time ahead of TSERVER 2's clock because of clock skew.

1. An INSERT of token 29 is issued to YSQL on TSERVER 1.
2. This INSERT is routed to the tablet hosted on TSERVER 3. The operation picks a commit time of `T2=103` even though the reference clock reads `98`.
3. TSERVER 1 acknowledges the INSERT.
4. Now that the INSERT command is complete, the SELECT command is issued.
5. TSERVER 1 starts a distributed read. TSERVER 1 reads data from multiple shards on different physical YB-TServers in the cluster, namely TSERVER 2 and TSERVER 3. The read point that defines the snapshot of the database at which the data will be read, is picked on TSERVER 2 based on the safe time of that YB-TServer, namely `T1=101`.
6. TSERVER 1 gets back token 17 in the result set.
7. TSERVER 1 now issues a read operation to TSERVER 3 to retrieve records from the second shard. However, since the read time `T1=101` is less than the commit time of the prior insert `T2=103`, the record is not part of the read snapshot.
8. Thus, token 29 is omitted from the result set returned.

  More generally, this anomaly occurs whenever the commit time of a prior write operation `T2` is higher than the read time `T1` of a later read operation, thus violating the _read-after-commit-visibility_ guarantee.

How does YugabyteDB prevent this clock skew anomaly?

* First, note that the clock skew between all nodes in the cluster is always in a [max_clock_skew_usec](../../../reference/configuration/yb-tserver/#max-clock-skew-usec) bound due to clock synchronization algorithms.
* Recall that the read operation has a read time of `T1`. For records with a commit timestamp later than `T1` + `max_clock_skew`, the database can be sure that these records were written after the read was issued and exclude it from the results. For records with commit timestamp less than `T1`, the database can include the record in the results, even when the write is concurrent with the read. But for records with a commit timestamp between `T1` and `T1` + `max_clock_skew`, the database cannot determine whether the record should be included or not because:

  * The read operation cannot determine whether the record is committed strictly before the read was issued because of clock skew. Therefore, it cannot simply exclude the record from its output.

  * However, the read operation cannot simply include all records in this ambiguity window because a consistent snapshot must be returned. That is, the read cannot simply advance its read time on observing a record with higher timestamp since the read already returned records from an older snapshot thus far. Therefore, the read must be restarted from the beginning with the advanced timestamp. Thus, the name read restart error.

* Whenever a read operation finds records with timestamp in the range `(T1, T1+max_clock_skew]`, to avoid breaking the strong guarantee that a reader should always be able to read what was committed earlier, and to read a consistent snapshot, the read operation raises a `Read restart` error to restart the read.

## Troubleshooting

You can handle and mitigate read restart errors using the following techniques:

- Implement retry logic in the application. Application retries can help mitigate read restart errors. Moreover, a statement or a transaction may fail in other ways such as transaction conflicts or infrastructure failures. Therefore, a retry mechanism is strongly recommended for a cloud-native, distributed database such as YugabyteDB.

  While implementing application retries is the best long-term approach, there are a few short-term solutions you can use in the interim.
- Use SERIALIZABLE READ ONLY DEFERRABLE mode when running background reads. Read restart errors usually occur when the query is a SELECT statement with a large output footprint and there are concurrent writes that satisfy the SELECT statement.

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
- In rare scenarios where neither latency nor memory can be compromised, but _read-after-commit-visibility_ guarantee is not a necessity, set `yb_read_after_commit_visibility` to `relaxed`. This option only affects pure reads.

  ```sql
  SET yb_read_after_commit_visibility TO relaxed;
  SELECT * FROM large_table;
  ```

  Please exercise caution when using this option.
