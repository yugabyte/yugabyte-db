---
title: Transactions in YSQL
headerTitle: Transactions
linkTitle: 4. Transactions
description: Learn how to use Transactions in YSQL on YugabyteDB.
aliases:
  - /preview/explore/transactional/acid-transactions/
  - /preview/develop/learn/acid-transactions/
menu:
  preview:
    identifier: acid-transactions-1-ysql
    parent: learn
    weight: 566
type: docs
---

{{<tabs>}}
{{<tabitem href="../acid-transactions-ysql/" text="YSQL" icon="postgres" active="true" >}}
{{<tabitem href="../acid-transactions-ycql/" text="YCQL" icon="cassandra" >}}
{{</tabs>}}

In YugabyteDB, a transaction is a sequence of operations performed as a single logical unit of work. The essential point of a transaction is that it bundles multiple steps into a single, all-or-nothing operation. The intermediate states between the steps are not visible to other concurrent transactions, and if some failure occurs that prevents the transaction from completing, then none of the steps affect the database at all.

In YugabyteDB, a transaction is the set of commands inside a `BEGIN - COMMIT` block. For example,

```plpgsql
BEGIN;
  UPDATE accounts SET balance = balance + 1000.00 WHERE name = 'John Smith';
  -- other statements
COMMIT;
```

The `BEGIN` and `COMMIT` block is needed when you have multiple statements to be executed as part of a transaction. YugabyteDB treats every ad-hoc individual SQL statement as being executed in a transaction.

If you decide to cancel the transaction and not commit it, you can issue a `ROLLBACK` instead of a `COMMIT`. You can also control the rollback of a subset of statements using `SAVEPOINT`. After rolling back to a savepoint, it continues to be defined, so you can roll back to it several times.

As all transactions in YugabyteDB are guaranteed to be [ACID](../../../../architecture/transactions/transactions-overview#acid-properties) compliant, errors can occur during transaction processing for various reasons. YugabyteDB returns different [error codes](../transactions-errorcodes-ysql) for each case with details. Applications need to be designed to do [error handling](../transactions-high-availability-ysql) correctly for high availability.

{{<tip title="In action">}}
For an example of how a transaction is run, see [Distributed transactions](../../../../explore/transactions/distributed-transactions-ysql).
{{</tip>}}

## Commands

The following commands are typically involved in a transaction flow:

| Command | Description | Example |
| ------: | :---------- | :------ |
|[BEGIN](../../../../api/ysql/the-sql-language/statements/txn_begin) | Start a transaction. This is the first statement in a transaction. | *BEGIN TRANSACTION* |
|[SET](../../../../api/ysql/the-sql-language/statements/cmd_set) | Set session-level transaction settings | SET idle_in_transaction_session_timeout = 10000 |
|[SHOW](../../../../api/ysql/the-sql-language/statements/cmd_show) | Display session-level transaction settings | SHOW idle_in_transaction_session_timeout |
|[SET TRANSACTION](../../../../api/ysql/the-sql-language/statements/txn_set) | Set the isolation level. | SET TRANSACTION SERIALIZABLE |
|[SAVEPOINT](../../../../api/ysql/the-sql-language/statements/savepoint_create) | Create a checkpoint. | SAVEPOINT yb_save|
|[ROLLBACK TO SAVEPOINT](../../../../api/ysql/the-sql-language/statements/savepoint_rollback) | Rollback to a specific savepoint. | ROLLBACK TO SAVEPOINT yb_save |
|[RELEASE SAVEPOINT](../../../../api/ysql/the-sql-language/statements/savepoint_release) | Destroy a savepoint. | RELEASE yb_save |
|[ROLLBACK](../../../../api/ysql/the-sql-language/statements/txn_rollback) | Cancel a transaction. | ROLLBACK |
|[COMMIT](../../../../api/ysql/the-sql-language/statements/txn_commit) | Apply the transaction to the tables. | COMMIT |

## Session-level settings

The following session-level settings affect transactions and can be configured to your application needs. These settings can be set using the [SET](../../../../api/ysql/the-sql-language/statements/cmd_set) command and the current values can be fetched using the [SHOW](../../../../api/ysql/the-sql-language/statements/cmd_show) command.

{{<note title="Note">}}
These settings impact all transactions in the current session only.
{{</note>}}

### default_transaction_read_only

Turn this setting `ON/TRUE/1` to make all the transactions in the current session read-only. This is helpful when you want to run reports or set up [follower reads](../transactions-performance-ysql#read-from-followers).

```plpgsql
SET default_transaction_read_only = TRUE;
```

### default_transaction_isolation

Set this to one of `serializable`, `repeatable read`, or `read committed `. This sets the default isolation level for all transactions in the current session.

```plpgsql
SET default_transaction_isolation = 'serializable';
```

### default_transaction_deferrable

Turn this setting `ON/TRUE/1` to make all the transactions in the current session [deferrable](../../../../api/ysql/the-sql-language/statements/txn_set/#deferrable-mode-1). This ensures that the transactions are not canceled by a serialization failure.

```plpgsql
SET default_transaction_deferrable = TRUE;
```

{{<note title="Note">}}
The `DEFERRABLE` transaction property has no effect unless the transaction is also `SERIALIZABLE` and `READ ONLY`.
{{</note>}}

### idle_in_transaction_session_timeout

Set this to a duration (for example, `'10s or 1000'`) to limit delays in transaction statements. Default time units is milliseconds. See [Handle idle transactions](../transactions-performance-ysql#handle-idle-applications).

### yb_transaction_priority_lower_bound

Set this to values in the range `[0.0 - 1.0]` to set the lower bound of the dynamic priority assignment. See [Optimistic concurrency control](../transactions-performance-ysql#optimistic-concurrency-control).

### yb_transaction_priority_upper_bound

Set this to values in the range `[0.0 - 1.0]` to set the upper bound of the dynamic priority assignment. See [Optimistic concurrency control](../transactions-performance-ysql#optimistic-concurrency-control).

## Isolation levels

Isolation level defines the level of data visibility to the transaction. YugabytedDB supports [multi-version concurrency control (MVCC)](../../../../architecture/transactions/transactions-overview/#multi-version-concurrency-control), which enables isolation to concurrent transactions without the need for locking.

YugabyteDB supports three kinds of isolation levels to support different application needs.

### Repeatable Read (Snapshot)

In [Repeatable Read / Snapshot](../../../../explore/transactions/isolation-levels/#snapshot-isolation) isolation level, only the data that is committed before the transaction began is visible to the transaction. Although updates done in the transaction are visible to any query in the transaction, the transaction does not see any changes made by other concurrent transactions. Effectively, the transaction sees the snapshot of the database as of the start of the transaction. Applications using this isolation level should be designed to retry on serialization failures.

### Read Committed

In [Read Committed](../../../../explore/transactions/isolation-levels/#read-committed-isolation) isolation level (currently in beta), each statement of the transaction sees the latest data committed by any concurrent transaction just before the execution of the statement. If another transaction has modified a row related to the current transaction, the current transaction waits for the other transaction to commit or rollback its changes. Here, the server internally waits and retries on conflicts, so applications need not worry about retrying on serialization failures.

### Serializable

[Serializable](../../../../explore/transactions/isolation-levels/#serializable-isolation) isolation level is the strictest. It has the effect of all transactions being executed in a serial manner, one after the other rather than in parallel. Applications using this isolation level should be designed to retry on serialization failures.

{{<tip title="Examples">}}
See [isolation level examples](../../../../explore/transactions/isolation-levels/) to understand the effect of these different levels of isolation.
{{</tip>}}

## Row-level locking

Typically `SELECT` statements do not automatically lock the rows fetched during a transaction. Depending on your application needs, you might have to lock the rows retrieved during `SELECT`. YugabyteDB supports [explicit row-level locking](../../../../explore/transactions/explicit-locking) for such cases and ensures that no two transactions can hold locks on the same row. Lock acquisition conflicts are resolved according to [concurrency control](../../../../architecture/transactions/concurrency-control/) policies.

Lock acquisition has the following format:

```plpgsql
SELECT * FROM txndemo WHERE k=1 FOR UPDATE;
```

YugabyteDB supports the following types of explicit row locks:

- FOR UPDATE - Strongest and exclusive lock. Prevents all other locks on these rows till the transaction ends.
- FOR NO KEY UPDATE - Weaker than `FOR UPDATE` and exclusive. Will not block `FOR KEY SHARE` commands.
- FOR SHARE - Shared lock that does not block other `FOR SHARE` and `FOR KEY SHARE` commands.
- FOR KEY SHARE - Shared lock that does not block other `FOR SHARE`, `FOR KEY SHARE`, and `FOR NO KEY UPDATE` commands.

{{<tip title="Examples">}}
For more details and examples related to these locking policies, see [Explicit locking](../../../../explore/transactions/explicit-locking).
{{</tip>}}

## Learn more

- [Transaction error codes](../transactions-errorcodes-ysql) - Various error codes returned during transaction processing.
- [Transaction error handling](../transactions-high-availability-ysql) - Methods to handle various error codes to design highly available applications.
- [Transaction isolation levels](../../../../architecture/transactions/isolation-levels/) - Various isolation levels supported by YugabyteDB.
- [Concurrency control](../../../../architecture/transactions/concurrency-control/) - Policies to handle conflicts between transactions.
- [Transaction priorities](../../../../architecture/transactions/transaction-priorities/) - Priority buckets for transactions.
- [Transaction options](../../../../explore/transactions/distributed-transactions-ysql/#transaction-options) - Options supported by transactions.
