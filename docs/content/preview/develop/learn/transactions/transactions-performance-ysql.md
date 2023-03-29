---
title: Performance Tuning Transactions in YSQL
headerTitle: Performance tuning in YSQL
linkTitle: Performance tuning
description: Learn how to speed up Transactions in YSQL on YugabyteDB.
menu:
  preview:
    identifier: transactions-performance-ysql
    parent: acid-transactions-1-ysql
    weight: 570
type: docs
---

As a versatile distributed database, YugabyteDB can be deployed in a variety of configurations for a variety of use cases. Let's see some best practices and tips that can hugely improve the performance of a YugabyteDB cluster.

{{<note title="Setup">}}
To run the following examples, first set up a cluster and database schema as described in [Prerequisites](../transactions-high-availability-ysql#prerequisites).
{{</note>}}


## Fast single-row transactions

YugabytedDB has specific optimizations to improve the performance of transactions in certain scenarios where transactions operate on a single row. These transactions are referred to as [single-row or fast path](../../../../architecture/transactions/single-row-transactions/) transactions. These are much faster than [distributed](../../../../architecture/transactions/distributed-txns/) transactions that impact a set of rows distributed across shards that are themselves spread across multiple nodes distributed across a data center, region, or globally.

For example, consider a common scenario in transactions where a single row is updated and the new value is fetched. This is usually done in multiple steps as follows:

```plpgsql
BEGIN;
SELECT v FROM txndemo WHERE k=1 FOR UPDATE;
UPDATE txndemo SET v = v + 3 WHERE k=1;
SELECT v FROM txndemo WHERE k=1;
COMMIT;
```

In this formulation, when the rows are locked in the first `SELECT` statement, YugabyteDB does not know what rows are going to be modified in subsequent commands. As a result, it considers the transaction to be distributed.

However, if you write it as a single statement, YugabyteDB can confidently treat it as a single-row transaction. To update a row and return its new value using a single statement, use the `RETURNING` clause as follows:

```plpgsql
UPDATE txndemo SET v = v + 3 WHERE k=1 RETURNING v;
```

YugabyteDB treats this as a single-row transaction, which executes much faster. This also saves one round trip and immediately fetches the updated value.


## Minimize conflict errors

The [INSERT](../../../../api/ysql/the-sql-language/statements/dml_insert/) statement has an optional [ON CONFLICT](../../../../api/ysql/the-sql-language/statements/dml_insert/#on-conflict-clause) clause that can be helpful to circumvent certain errors and avoid multiple statements.

For example, if concurrent transactions are inserting the same row, this could cause a UniqueViolation. Instead of letting the server throw an error and handling it in code, you could just ask the server to ignore it as follows:

```plpgsql
INSERT INTO txndemo VALUES (1,10) DO NOTHING;
```

With [DO NOTHING](../../../../api/ysql/the-sql-language/statements/dml_insert/#conflict-action-1), the server does not throw an error, resulting in one less round trip between the application and the server.

You can also simulate an `upsert` by using `DO UPDATE SET` instead of doing a `INSERT`, fail, and `UPDATE`, as follows:

```plpgsql
INSERT INTO txndemo VALUES (1,10) 
        DO UPDATE SET v=10 WHERE k=1;
```

Now, the server automatically updates the row when it fails to insert. Again, this results in one less round trip between the application and the server.


## Avoid long waits

In [READ COMMITTED isolation level](../../../../architecture/transactions/read-committed/), clients do not need to retry or handle serialization errors. During conflicts, the server retries indefinitely based on the [retry options](../../../../architecture/transactions/read-committed/#performance-tuning) and [Wait-On-Conflict](../../../../architecture/transactions/concurrency-control/#wait-on-conflict) policy.

To avoid getting stuck in a wait loop because of starvation, you should use a reasonable timeout for the statements, similar to the following:

```plpgsql
SET statement_timeout = '10s';
```

This ensures that the transaction would not be blocked for more than 10 seconds.


## Handle idle applications

When an application takes a long time between two statements in a transaction or just hangs, it could be holding the locks on the [provisional records](../../../../architecture/transactions/distributed-txns/#provisional-records) during that period. It would hit a timeout if the `idle_in_transaction_session_timeout` is set accordingly. After that timeout is reached, the connection is disconnected and the client would have to reconnect. The typical error message would be:

```output
FATAL:  25P03: terminating connection due to idle-in-transaction timeout
```

By default, the `idle_in_transaction_session_timeout` is set to `0`. You can set the timeout to a specific value in [ysqlsh](../../../../admin/ysqlsh/#starting-ysqlsh) using the following command:

```plpgsql
SET idle_in_transaction_session_timeout = '10s';
```

To view the current value, use the following command:

```plpgsql
SHOW idle_in_transaction_session_timeout;
```

```output
 idle_in_transaction_session_timeout
-------------------------------------
 10s
```

Setting this timeout can avoid deadlock scenarios where applications acquire locks and then hang unintentionally.


## Long scans and batch jobs

When a transaction is in `SERIALIZABLE` isolation level and `READ ONLY` mode, if the transaction property `DEFERRABLE` is set, then that transaction executes with much lower overhead and is never canceled because of a serialization failure. This can be used for batch or long-running jobs, which need a consistent snapshot of the database without interfering or being interfered with by other transactions. For example:

```plpgsql
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE READ ONLY DEFERRABLE;
SELECT * FROM very_large_table;
COMMIT;
```

## Server side cursors

Cursors allow applications to efficiently scan through and process large data sets, without having to re-execute the query again and again. Additionally, cursors allow distributed data processing on the application side. Multiple queries can be executed to a single database backend, which allows processing subsets of the data in parallel. For example, to fetch results in batches of `10`, one would normally use `LIMIT 10` and increment the `OFFSET` by `10` on every query like:

```plpgsql
SELECT * FROM txndemo ORDER BY k DESC LIMIT 10 OFFSET 10;
```

But this would result in re-executing the query and loading the data from disk every time. This can be avoided using [cursors](../../../../explore/ysql-language-features/advanced-features/cursor/) which would maintain the pointer to the current position of the query on the server side. You could create a cursor like:

```
DECLARE fast_scan CURSOR FOR SELECT * FROM txndemo ORDER BY k DESC;
```

and fetch the next `10` items as:

```
FETCH 10 FROM fast_scan;
```

This would vastly reduce the time taken for multiple fetches.


## Optimistic concurrency control

As noted, all transactions are dynamically assigned a priority. This is a value in the range of `[0.0, 1.0]`. The current priority can be fetched using the `yb_transaction_priority` setting as follows:

```plpgsql
SHOW yb_transaction_priority;
```

```output
          yb_transaction_priority
-------------------------------------------
 0.000000000 (Normal priority transaction)
```

The priority value is bound by two settings, namely `yb_transaction_priority_lower_bound` and `yb_transaction_priority_upper_bound`. If an application would like a specific transaction to be given higher priority, it can issue statements like the following:

```plpgsql
SET yb_transaction_priority_lower_bound=0.9;
SET yb_transaction_priority_upper_bound=1.0;
```

This ensures that the priority assigned to your transaction is in the range `[0.9-1.0]` and thereby making it a high priority transaction.


## Stored Procedures : Minimize round trips

A transaction block executed from the client that has multiple statements would involve multiple round trips between the client and the server. Consider the following transaction:

```plpgsql
BEGIN TRANSACTION;
    UPDATE txndemo SET v = 11 WHERE k = 1;
    UPDATE txndemo SET v = 22 WHERE k = 2;
    UPDATE txndemo SET v = 33 WHERE k = 3;
COMMIT;
```

This would entail `5` round trips between the application and server, which means `5` times the latency between the application and the server. This would be very detrimental even if the latency between them is low. These round trips could be avoided if these transactions are wrapped in a [stored procedure](../../../../api/ysql/the-sql-language/statements/ddl_create_function/). A stored procedure is executed in the server and can incorporate loops and error handling. Stored procedures can be invoked from the client in just one call as follows:

```sql
CALL stored_procedure_name(argument_list);
```

Depending on the complexity of your transaction block, this can vastly improve the performance.


## Place leaders in one region

In a [multi-region](../../../../explore/multi-region-deployments/) setup, a transaction would have to reach out to the tablet leaders spread across multiple regions. In this scenario, the transaction can incur high inter-regional latencies that could multiply with the number of statements that have to travel cross-region.

Cross-region trips can be avoided by placing all the tablet leaders in one region using the [set_preferred_zones](../../../../admin/yb-admin/#set-preferred-zones) command in [yb-admin](../../../../admin/yb-admin).

You can also do this by [marking the zones as Preferred](../../../../yugabyte-platform/manage-deployments/edit-universe/) on the **Edit Universe** page in [YugabyteDB Anywhere](../../../../yugabyte-platform/), or [setting the region as preferred](../../../../yugabyte-cloud/cloud-basics/create-clusters/create-clusters-multisync/#preferred-region) in YugabyteDB Managed.


## Read from followers

All reads in YugabyteDB are handled by the leader to ensure that applications fetch the latest data, even though the data is replicated to the followers. While replication is fast, it is not instantaneous, and the followers may not have the latest data at the read time. But in some scenarios, reading from the leader is not necessary. For example:

- The data does not change often (for example, a movie database).
- The application does not need the latest data (for example, reading yesterday's report).

In such scenarios, you can enable [follower reads](../../../../explore/ysql-language-features/going-beyond-sql/follower-reads-ysql/) to read from followers instead of going to the leader, which could be far away in a different region.

To enable follower reads, set the transaction to be `READ ONLY` and turn on the session-level setting `yb_read_from_followers`. For example:

```plpgsql
SET yb_read_from_followers = true;
BEGIN TRANSACTION READ ONLY;
...
COMMIT;
```

{{<note title="Note">}}
Follower reads only affects reads. All writes are still handled by the leader.
{{</note>}}


## Learn more

- [Transaction error codes](../transactions-errorcodes-ysql) - Various error codes returned during transaction processing.
- [Transaction error handling](../transactions-high-availability-ysql) - Methods to handle various error codes to design highly available applications.
- [Transaction isolation levels](../../../../architecture/transactions/isolation-levels/) - Various isolation levels supported by YugabyteDB.
- [Concurrency control](../../../../architecture/transactions/concurrency-control/) - Policies to handle conflicts between transactions.
- [Transaction priorities](../../../../architecture/transactions/transaction-priorities/) - Priority buckets for transactions.
- [Transaction options](../../../../explore/transactions/distributed-transactions-ysql/#transaction-options) - Options supported by transactions.
