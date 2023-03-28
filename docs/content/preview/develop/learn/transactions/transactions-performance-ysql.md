---
title: Performance Tuning of Transactions in YSQL
headerTitle: Performance Tuning
linkTitle: Performance Tuning
description: Learn how to use speed up Transactions in YSQL on YugabyteDB.
menu:
  preview:
    identifier: transactions-performance-ysql
    parent: acid-transactions-1-ysql
    weight: 570
type: docs
---

YugabyteDB being versatile distributed database, can be deployed in various configurations and is used for a variety of use cases. Let's see some best practices and tips that can hugely improve the performance of a YugabyteDB cluster.

{{<note title="Setup">}}
All the examples mentioned below would need the [prerequisite schema and cluster](../transactions-high-availability-ysql#prerequisites) setup prior to execution.
{{</note>}}

## Fast Single-row transactions

YugabytedDB has specific optimizations to improve the performance of transactions in certain scenarios where transactions operate on a single row. These transactions are referred to as [single-row or fast path](../../../../architecture/transactions/single-row-transactions/) transactions. These are much faster than [distributed](../../../../architecture/transactions/distributed-txns/) transactions that impact a set of rows distributed across shards that are themselves spread across multiple nodes distributed across a data center, region or globally.

For example, consider a common scenario in transactions where a single row is updated and the new value is fetched. This is usually done in multiple steps like:

```plpgsql
BEGIN;
SELECT v FROM txndemo WHERE k=1 FOR UPDATE;
UPDATE txndemo SET v = v + 3 WHERE k=1;
SELECT v FROM txndemo WHERE k=1;
COMMIT;
```

The issue with the above code block is, when the rows are locked in the first `SELECT` statement, YugabyteDB does not know what rows are going to be modified in the further commands. So, naturally it would consider it to be [distributed transaction](../../../../architecture/transactions/distributed-txns/). But if it is just a single statement, YugabyteDB would have been able to confidently figure out that it was a single-row transaction. To update a row and return it's new value, we can do it in a single statement using the `RETURNING` clause as follows:

```plpgsql
UPDATE txndemo SET v = v + 3 WHERE k=1 RETURNING v;
```

Now, YugabyteDB will treat this as a [single-row](../../../../architecture/transactions/single-row-transactions/) transaction and would execute much faster. This also saves one round trip and immediately fetches the updated value.


## Minimize conflict errors

The [INSERT](../../../../api/ysql/the-sql-language/statements/dml_insert/) statement has an optional [ON CONFLICT](../../../../api/ysql/the-sql-language/statements/dml_insert/#on-conflict-clause) clause that can be helpful to circumvent certain errors and avoid multiple statements.

For example, if concurrent transactions are inserting the same row, this could cause a UniqueViolation. Instead of letting the server throw an error and handling it in code, you could just ask the server to ignore it as follows:

```plpgsql
INSERT INTO txndemo VALUES (1,10) DO NOTHING;
```

With [DO NOTHING](../../../../api/ysql/the-sql-language/statements/dml_insert/#conflict-action-1), the server does not throw an error, resulting in one less round-trip between the application and the server.

You can also simulate an `upsert` by using `DO UPDATE SET` instead of doing a `insert`, fail, and `update` scenario, as follows:

```plpgsql
INSERT INTO txndemo VALUES (1,10) 
        DO UPDATE SET v=10 WHERE k=1;
```

Now, the server automatically updates the row when it fails to insert. Again, this results in one less round-trip between the application and the server.


## Avoid long waits

In [READ COMMITTED isolation level](../../../../architecture/transactions/read-committed/), clients do not need to retry or handle serialization errors. During conflicts, the server retries indefinitely based on the [retry options](../../../../architecture/transactions/read-committed/#performance-tuning) and [Wait-On-Conflict](../../../../architecture/transactions/concurrency-control/#wait-on-conflict) policy.

To avoid getting stuck in a wait loop because of starvation, it is recommended to use a reasonable timeout for the statements similar to the following:

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

## Large scans and batch jobs

When a transaction is in `SERIALIZABLE` isolation level and `READ ONLY` mode, if the transaction property `DEFERRABLE` is set, then that transaction executes with much lower overhead and is never canceled because of a serialization failure. This can be used for batch or long-running jobs, which need a consistent snapshot of the database without interfering or being interfered with by other transactions. For example:

```plpgsql
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE READ ONLY DEFERRABLE;
SELECT * FROM very_large_table;
COMMIT;
```

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


## Minimizing round trips

A transaction block executed from the client has multiple statements would involve multiple round trips between the client and the server. These round trips can be avoided if these transactions are wrapped in a [stored procedure](../../../../api/ysql/the-sql-language/statements/ddl_create_function/). The whole stored procedures is executed within the server which could involve loops and error handling. Stored procedures can be invoked from the client like:

```sql
CALL stored_procedure_name(argument_list);
```

Depending one the complexity of your transaction block, this could vastly improve the performance.


## Leaders in one region

In a [multi-region](../../../../explore/multi-region-deployments/) setup, a transaction would have to reach out the tablet leaders spread across multiple regions. In such a scenario, the transaction could incur high inter-regional latencies that could multiply with the no.of statements that have to go cross-region. 

The cross-region trips can be avoided by enforcing all the leaders to be in one region using the [set_preferred_zones](../../../../admin/yb-admin/#set-preferred-zones) command in [yb-admin](../../../../admin/yb-admin) or using the checking the appropriate [Preferred check boxes](../../../../yugabyte-platform/manage-deployments/edit-universe/) on the **Edit Universe** page in [YugabyteDB Platform](../../../../yugabyte-platform/)


## Read from followers

All reads in YugabyteDB are handled by the leader to ensure that the applications fetch the latest data, even though the data is replicated to the followers. Replication is fast, but not instantaneous. So all the followers may not have the latest data at the read time. But there are a few scenarios where reading from the leader is not necessary, like:
1. The data does not change often (eg. Movie Database)
1. The application does not need the latest data. (eg. Yesterday’s report)

In such scenarios, you can enable [Follower Reads](../../../../explore/ysql-language-features/going-beyond-sql/follower-reads-ysql/) in YugabyteDB to read from followers instead of going to the leader, which could be far away in a different region. To enable this, all you have to do is to set the transaction to be read_only and turn ON a session-level setting `yb_read_from_followers` like:

```plpgsql
SET yb_read_from_followers = true;
BEGIN TRANSACTION READ ONLY;
...
COMMIT;
```

{{<note title="Note">}}
Follower reads works only for reads. All **writes** will still go to the leader.
{{</note>}}


## Learn more

- [Transaction error codes](../transactions-errorcodes-ysql) - Various error codes returned during transaction processing.
- [Transaction error handling](../transactions-high-availability-ysql) - Methods to handle various error codes to design highly available applications.
- [Transaction isolation levels](../../../../architecture/transactions/isolation-levels/) - Various isolation levels supported by YugabyteDB.
- [Concurrency control](../../../../architecture/transactions/concurrency-control/) - Policies to handle conflicts between transactions.
- [Transaction priorities](../../../../architecture/transactions/concurrency-control/) - Priority buckets for transactions.
- [Transaction options](../../../../explore/transactions/distributed-transactions-ysql/#transaction-options) - Options supported by transactions.
