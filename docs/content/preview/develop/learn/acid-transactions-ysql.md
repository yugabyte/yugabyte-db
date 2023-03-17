---
title: Transactions in YSQL
headerTitle: Transactions
linkTitle: 4. Transactions
description: Learn how Transactions work in YSQL on YugabyteDB.
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

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../acid-transactions-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../acid-transactions-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

In YugabyteDB, a transaction is a sequence of operations performed as a single logical unit of work. A transaction has four key properties - **Atomicity**, **Consistency**, **Isolation**, and **Durability** - commonly abbreviated as **ACID**.

- **Atomicity** All the work in a transaction is treated as a single atomic unit - either all of it is performed or none of it is.

- **Consistency** A completed transaction leaves the database in a consistent internal state. This can either be all the operations in the transactions succeeding or none of them succeeding.

- **Isolation** This property determines how/when changes made by one transaction become visible to the other. For example, a *serializable* isolation level guarantees that two concurrent transactions appear as if one executed after the other (that is, as if they occur in a completely isolated fashion). YugabyteDB supports *Snapshot*, *Serializable*, and *Read Committed* isolation levels. Read more about the different [levels of isolation](../../../architecture/transactions/isolation-levels/).

- **Durability** The results of the transaction are permanently stored in the system. The modifications must persist even in the instance of power loss or system failures.

## Types of transactions

YugabytedDB provides strong [ACID](#ACID) guarantees on three categories of transactions:

1. [**Single-row**](../../../architecture/transactions/single-row-transactions/) transactions are transactions where all operations impact a single row (that is, a key). Single-row ACID is easier to achieve as, in most distributed databases, the data for a single row doesn’t cross into other nodes.

1. [**Single-shard**](../../../architecture/transactions/single-row-transactions/) transactions occur when all rows involved in the transaction's operations exist in a single shard (which lies in a single node) of a distributed database.

1. [**Distributed**](../../../architecture/transactions/distributed-txns/) transactions impact a set of rows distributed across shards that are themselves spread across multiple nodes distributed across a data center, region or globally.

YugabyteDB has separate optimizations for each to make them faster.

## Commands

The following commands are typically involved in a transaction flow:

- [BEGIN](../../../api/ysql/the-sql-language/statements/txn_begin) - Start a transaction.
- [SET](../../../api/ysql/the-sql-language/statements/cmd_set) - Set configuration values
- [SET TRANSACTION](../../../api/ysql/the-sql-language/statements/txn_set) - Set the isolation level.
- [SHOW](../../../api/ysql/the-sql-language/statements/cmd_show) - Display configuration values.
- [SAVEPOINT](../../../api/ysql/the-sql-language/statements/savepoint_create) - Create a checkpoint.
- [ROLLBACK TO SAVEPOINT](../../../api/ysql/the-sql-language/statements/savepoint_rollback) - Rollback to a specific savepoint.
- [RELEASE SAVEPOINT](../../../api/ysql/the-sql-language/statements/savepoint_release) - Destroy a savepoint.
- [ROLLBACK](../../../api/ysql/the-sql-language/statements/txn_rollback) - Rollback a transaction.
- [COMMIT](../../../api/ysql/the-sql-language/statements/txn_commit) - Commit a transaction.

## Session-level settings

The following session-level settings affect transactions and can be configured to your needs:

- `transaction_read_only` = `[on|off]` - Set this to `on` to make all transactions in the current session read-only.
- `transaction_isolation` = `[serializable|repeatable read|read committed]` - Set the default isolation level for all transactions in the current session.
- `transaction_deferrable` = `[on|off]` - Set this to `on` to make all transactions in the current session [deferrable](#deferrable-property).
- `idle_in_transaction_session_timeout` - Set this to a duration (for example, `'10s or 1000'`) to limit delays in transaction statements. Default time units is milliseconds. Refer to [Idle timeout](#idle-timeout).
- `yb_transaction_priority_lower_bound` = `[0.0-1.0]` - Set the lower bound of the dynamic priority assignment.
- `yb_transaction_priority_upper_bound` = `[0.0-1.0]` - Set the upper bound of the dynamic priority assignment.

These settings impact all transactions in the current session only.

## Isolation levels

Isolation level defines the level of data visibility to the transaction.YugabytedDB supports [multi-version concurrency control (MVCC)](../../../architecture/transactions/transactions-overview/#multi-version-concurrency-control), which enables isolation to concurrent transactions without the need for locking.

YugabyteDB supports three kinds of isolation levels to support different application needs.

### Repeatable Read (Snapshot)

In [Repeatable Read / Snapshot](../../../explore/transactions/isolation-levels/#snapshot-isolation) isolation level, only the data that is committed before the transaction began is visible to the transaction. Although updates done in the transaction are visible to any query in the transaction, the transaction does not see any changes made by other concurrent transactions. Effectively, the transaction sees the snapshot of the database as of the start of the transaction. Applications using this isolation level should be designed to retry on serialization failures.

### Read Committed

In [Read Committed](../../../explore/transactions/isolation-levels/#read-committed-isolation) isolation level (currently in beta), each statement of the transaction sees the latest data committed by any concurrent transaction just before the execution of the statement. If another transaction has modified a row related to the current transaction, the current transaction waits for the other transaction to commit or rollback its changes. Here, the server internally waits and retries on conflicts, so applications need not worry about retrying on serialization failures.

### Serializable

[Serializable](../../../explore/transactions/isolation-levels/#serializable-isolation) isolation level is the strictest. It has the effect of all transactions being executed in a serial manner, one after the other rather than in parallel. Applications using this isolation level should be designed to retry on serialization failures.

## Row-level locking

Typically `SELECT` statements do not automatically lock the rows fetched during a transaction. Depending on your application needs, you might have to lock the rows retrieved during `SELECT`. YugabyteDB supports [explicit row-level locking](../../../explore/transactions/explicit-locking) for such cases and ensures that no two transactions can hold locks on the same row. Lock acquisition conflicts are resolved according to [concurrency control](../../../architecture/transactions/concurrency-control/) policies.

Lock acquisition has the following format:

```plpgsql
SELECT * FROM txndemo WHERE k=1 FOR UPDATE;
```

YugabyteDB supports the following types of explicit row locks:

- FOR UPDATE - Strongest and exclusive lock. Prevents all other locks on these rows till the transaction ends.
- FOR NO KEY UPDATE - Weaker than `FOR UPDATE` and exclusive. Will not block `FOR KEY SHARE` commands.
- FOR SHARE - Shared lock that does not block other `FOR SHARE` and `FOR KEY SHARE` commands.
- FOR KEY SHARE - Shared lock that does not block other `FOR SHARE`, `FOR KEY SHARE`, and `FOR NO KEY UPDATE` commands.

## Transaction error codes

Due to the strong [ACID](#ACID) guarantees, failures during transactions are inevitable. You need to design your applications to take appropriate actions on the failed statements to ensure they are highly available. YugabyteDB returns various [error codes](#transaction-error-codes) for errors that occur during transaction processing.

The following error codes typically occur during transaction processing.

### 25001: Active SQL transaction

This error occurs when certain statements that should be run inside a transaction block are executed in a transaction block, typically because they have non-rollback-able side effects or do internal commits. For example, issuing a `BEGIN` statement inside a transaction.

```output
WARNING:  25001: there is already a transaction in progress
```

### 25006: Read only SQL transaction

This error occurs when certain statements are executed in a read only transaction that violate the read only constraint. For example, modifying records inside a read only transaction.

```output
ERROR:  25006: cannot execute UPDATE in a read-only transaction
```

### 25P01: No active SQL Transaction

This error occurs when certain statements that should be executed in a transaction are executed outside of a transaction. For example, issuing a `ROLLBACK` outside a transaction.

```output
WARNING:  25P01: there is no transaction in progress
```

### 25P02: In failed SQL transaction

This error occurs when statements have failed inside a transaction and another statement other than `COMMIT` or `ROLLBACK` is executed.

```output
ERROR:  25P02: current transaction is aborted, commands ignored until end of transaction block
```

### 25P03: Idle in transaction session timeout

This occurs when an application stays idle longer than `idle_in_transaction_session_timeout` in the middle of a transaction.

```output
FATAL:  25P03: terminating connection due to idle-in-transaction timeout
```

### 40001: Serialization failure

This error occurs when a transaction cannot be applied or progress further because of other conflicting transactions. For example, when multiple transactions are modifying the same key.

```output
ERROR:  40001: Operation expired: Transaction XXXX expired or aborted by a conflict
```

### 2D000: Invalid transaction termination

This error occurs when a transaction is terminated either by a `COMMIT` or a `ROLLBACK` in an invalid location. For example, when a `COMMIT` is issued inside a stored procedure that is called from inside a transaction.

```output
ERROR:  2D000: invalid transaction termination
```

### 3B001: Invalid savepoint specification

This error occurs when you try to `ROLLBACK` to or `RELEASE` a savepoint that has not been defined.

```output
ERROR:  3B001: savepoint "FIRST_SAVE" does not exist
```

## Error handling

Although some errors are very specific to certain [transaction isolation levels](../../../explore/transactions/isolation-levels/), most errors are common across multiple isolation levels.

In general, the error codes can be classified into the following three types:

1. WARNING. Informational messages that explain why a statement failed. For example:

    ```output
    -- When a BEGIN statement is issued inside a transaction
    WARNING:  25001: there is already a transaction in progress
    ```

    Most client libraries hide warnings but you might notice the messages when you execute statements directly from a terminal. The statement execution can continue without interruption but would need to be modified to avoid the re-occurence of the message.

1. ERROR: Errors are returned when a transaction cannot continue and has to be restarted by the client. For example:

    ```output
    -- When multiple transactions are modifying the same key.
    ERROR:  40001: Operation expired: Transaction XXXX expired or aborted by a conflict
    ```

    These errors need to be handled by the application to take appropriate action.

1. FATAL. Fatal messages are returned to notify that the connection to a server has been disconnected. For example:

    ```output
    -- When the application takes a long time to issue a statement in the middle of a transaction.
    FATAL:  25P03: terminating connection due to idle-in-transaction timeout
    ```

    At this point, the application should reconnect to the server.

The examples in the following sections illustrate failure scenarios and techniques you can use to handle these failures in your applications.

### Prerequisites

Follow the [setup instructions](../../../explore#tabs-00-00) to start a single local YugabytedDB instance, and create a table as follows:

1. Create the table as follows:

    ```plpgsql
    CREATE TABLE txndemo (
      k int,
      V int,
      PRIMARY KEY(k)
    );
    ```

1. Add some data to the table as follows:

    ```plpgsql
    INSERT INTO txndemo VALUES (1,10),(2,10),(3,10),(4,10),(5,10);
    ```

## Transaction retries

### Automatic retries

YugabyteDB will retry failed transactions automatically on the server side whenever possible without client intervention as per the [concurrency control policies](../../../architecture/transactions/concurrency-control/#best-effort-internal-retries-for-first-statement-in-a-transaction). These happen even on single statements, which are implicitly considered as transactions. In [Read Committed](/#statement-timeout) isolation mode, the server retries indefinitely.

In some scenarios, a server-side retry is not suitable. For example, the retry limit has been reached or the transaction is not in a valid state. In these cases, it is the client's responsibility to retry the transaction at the application layer.

### Client-side retry

Most transaction errors that happen due to conflicts and deadlocks can be restarted by the client. The following scenarios describe the causes for failures, and the required methods to be handled by the applications.

Execute the transaction in a `try..catch` block in a loop. When a re-tryable failure happens, issue `ROLLBACK` and then retry the transaction. To avoid overloading the server and ending up in an indefinite loop, wait for a period of time between retires and limit the number of retries. The following illustrates a typical client-side retry implementation:

```python
max_attempts = 10   # max no.of retries
sleep_time = 0.002  # 2 ms - base sleep time
backoff = 2         # exponential multiplier

attempt = 0
while attempt < max_attempts:
    attempt += 1
    try :
        cursor = cxn.cursor()

        # Execute Transaction Statments here

        cursor.execute("COMMIT");
        break
    except psycopg2.errors.SerializationFailure as e:
        cursor.execute("ROLLBACK")
        if attempt < max_attempts:
            time.sleep(sleep_time)
            sleep_time *= backoff
```

If the `COMMIT` is successful, the program exits the loop. `attempt < max_attempts` limits the number of retries to `max_attempts`, and the amount of time the code waits before the next retry also increases with `sleep_time *= backoff`. Choose values as appropriate for your application.

### 40001 - SerializationFailure

SerializationFailure errors happen when multiple transactions are updating the same set of keys (conflict) or when transactions are waiting on each other (deadlock). The error messages could be one of the following types:

- During a conflict, certain transactions are retried. However, after the retry limit is reached, an error occurs as follows:

    ```output
    ERROR:  40001: All transparent retries exhausted.
    ```

- All transactions are given a dynamic priority. When a deadlock is detected, the transaction with lower priority is automatically killed. For this scenario, the client might receive a message similar to the following:

    ```output
    ERROR:  40001: Operation expired: Heartbeat: Transaction XXXX expired or aborted by a conflict
    ```

The correct way to handle this error is with a retry loop with exponential backoff, as described in [Client-side retry](#client-side-retry). For example:

```python
connstr = 'postgresql://yugabyte@localhost:5433/yugabyte'
cxn = psycopg2.connect(connstr)

max_attempts = 10   # max no.of retries
sleep_time = 0.002  # 2 ms - base sleep time
backoff = 2         # exponential multiplier

attempt = 0
while attempt < max_attempts:
    attempt += 1
    try :
        cursor = cxn.cursor()
        cursor.execute("BEGIN ISOLATION LEVEL SERIALIZABLE");
        cursor.execute("UPDATE txndemo SET v=20 WHERE k=1;");
        cursor.execute("COMMIT");
        break
    except psycopg2.errors.SerializationFailure as e:
        print(e)
        cursor.execute("ROLLBACK")
        if attempt < max_attempts:
            time.sleep(sleep_time)
            sleep_time *= backoff
```

When the `UPDATE` or `COMMIT` fails because of `SerializationFailure`, the code retries after waiting for `sleep_time` seconds, up to `max_attempts`.

### 25P02 - InFailedSqlTransaction

This error occurs when a statement is issued after there's already an error in a transaction. The error message would be similar to the following:

```output
ERROR:  25P02: current transaction is aborted, commands ignored until end of transaction block
```

The only valid statements at this point would be `ROLLBACK` or `COMMIT`. To handle this error, resolve the actual error and then issue a rollback. Consider the following scenario:

```python
connstr = 'postgresql://yugabyte@localhost:5433/yugabyte'
cxn = psycopg2.connect(connstr)
cursor = cxn.cursor()
try:
    cursor.execute("BEGIN")
    try:
        # forcing an error with invalid syntax
        cursor.execute("INVALID TXN STATEMENT;")
    except Exception as e:
        # ignoring the Syntax Error (!! WRONG !!)
        # Syntax errors should NOT be ignored. - Only for demo
        pass

    # the transaction is in an invalid state
    cursor.execute("UPDATE txndemo SET v=20 WHERE k=1")
    cursor.execute("COMMIT")
except psycopg2.errors.InFailedSqlTransaction as e:
  print(e)
  cursor.execute("ROLLBACK")
```

The `INVALID TXN STATEMENT` would throw a `SyntaxError` exception. The code (in-correctly) catches this and ignores it. The next `update` statement, even though valid, will fail with an `InFailedSqlTransaction` exception. This could be avoided by handling the actual error and issuing a `ROLLBACK`. In this case, a retry would not help as it is a SyntaxError, but there might be scenarios, where the transaction would succeed when retried. Another way would be to rollback to a checkpoint before the failed statement and proceed further as decribed in [using savepoints](#using-savepoints) section.

### Use savepoints

[Savepoints](../../../api/ysql/the-sql-language/statements/savepoint_create) are named checkpoints that are helpful to rollback just a few statements in case of error scenarios and proceed the transaction further rather than aborting the entire transaction.

Consider the following example that inserts a row `[k=1, v=30]`:

```python
connstr = 'postgresql://yugabyte@localhost:5433/yugabyte'
cxn = psycopg2.connect(connstr)
cursor = cxn.cursor()
try:
    cursor.execute("BEGIN")

    # ... Execute other statements

    cursor.execute("SAVEPOINT before_insert")
    try:
        # insert a row
        cursor.execute("INSERT INTO txndemo VALUES (1,30)")
    except psycopg2.errors.UniqueViolation as e:
        print(e)
        # k=1 already exists in our table
        cursor.execute("ROLLBACK TO SAVEPOINT before_insert")
        cursor.execute("UPDATE txndemo SET v=30 WHERE k=1;")

    # ... Execute other statements
    cursor.execute("COMMIT")
except Exception as e:
  print(e)
  cursor.execute("ROLLBACK")
```

If the row `[k=1]` already exists in the table, the `insert` would result in a UniqueViolation exception. Technically, the transaction would be in an error state and further statements would result in a [25P02: In failed sql transaction](#25P02-in-failed-sql-transaction) error. You have to catch the exception and rollback. But instead of rolling back the entire transaction, you can rollback to the previously declared savepoint, `before_insert` and update the value of the row with `k=1`. Then you can continue with other statements in the transaction.

## Non-retriable errors

Although most transactions can be retried in most error scenarios, there are cases where retrying a transaction will not resolve an issue. For example, errors can occur when statements are issued out of place. These statements have to be fixed in code to continue further.

### 25001 - Specify transaction isolation level

Transaction level isolation should be specified before the first statement of the transaction is executed. If not the following error occurs:

```plpgsql
BEGIN;
```

```output
BEGIN
Time: 0.797 ms
```

```plpgsql
UPDATE txndemo SET v=20 WHERE k=1;
```

```output
UPDATE 1
Time: 10.416 ms
```

```plpgsql
SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
```

```output
ERROR:  25001: SET TRANSACTION ISOLATION LEVEL must be called before any query
Time: 3.808 ms
```

### 25006 - Modify a row in a read-only transaction

This error occurs when a row is modified after specifying a transaction to be `READ ONLY` as follows:

```plpgsql
BEGIN READ ONLY;
```

```output
BEGIN
Time: 1.095 ms
```

```plpgsql
UPDATE txndemo SET v=20 WHERE k=1;
```

```output
ERROR:  25006: cannot execute UPDATE in a read-only transaction
Time: 4.417 ms
```

## Best practices for high availability

### On conflict

The [INSERT](../../../api/ysql/the-sql-language/statements/dml_insert/) statement has an optional [ON CONFLICT](../../../api/ysql/the-sql-language/statements/dml_insert/#on-conflict-clause) clause that can be helpful to circumvent certain errors and avoid multiple statements.

For example, if concurrent transactions are inserting the same row, this could cause a UniqueViolation. Instead of letting the server throw an error and handling it in code, you could just ask the server to ignore it as follows:

```plpgsql
INSERT INTO txndemo VALUES (1,10) DO NOTHING;
```

With [DO NOTHING](../../../api/ysql/the-sql-language/statements/dml_insert/#conflict-action-1), the server does not throw an error, resulting in one less round-trip between the application and the server.

You can also simulate an `upsert` by using `DO UPDATE SET` instead of doing a `insert`, fail, and `update` scenario, as follows:

```plpgsql
INSERT INTO txndemo VALUES (1,10) 
        DO UPDATE SET v=10 WHERE k=1;
```

Now, the server automatically updates the row when it fails to insert. Again, this results in one less round-trip between the application and the server.

### Returning on insert/update

To fetch the row data that was inserted or updated in a single statement (instead of using `update` and then a `select`), use the `RETURNING` clause as follows:

```plpgsql
UPDATE txndemo SET v = v + 3 WHERE k=1 RETURNING v;
```

```output
 v
----
 33
(1 row)
```

This saves one round trip and immediately fetches the updated value.

### Statement timeout

In [READ COMMITTED isolation level](../../../architecture/transactions/read-committed/), clients do not need to retry or handle serialization errors. During conflicts, the server retries indefinitely based on the [retry options](../../../architecture/transactions/read-committed/#performance-tuning) and [Wait-On-Conflict](../../../architecture/transactions/concurrency-control/#wait-on-conflict) policy.

To avoid getting stuck in a wait loop because of starvation, it is recommended to use a reasonable timeout for the statements similar to the following:

```plpgsql
SET statement_timeout = '10s';
```

This ensures that the transaction would not be blocked for more than 10 seconds.

### Idle timeout

When an application takes a long time between two statements in a transaction or just hangs, it could be still holding the locks on the [provisional records](../../../architecture/transactions/distributed-txns/#provisional-records) during that period. It would hit a timeout if the `idle_in_transaction_session_timeout` is set accordingly. After that timeout is reached, the connection is disconnected and the client would have to reconnect. The typical error message would be:

```output
FATAL:  25P03: terminating connection due to idle-in-transaction timeout
```

By default, the `idle_in_transaction_session_timeout` is set to `0`. You can set the timeout to a specific value in [ysqlsh](../../../admin/ysqlsh/#starting-ysqlsh) using the following command:

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

### Deferrable property

When a transaction is in `SERIALIZABLE` isolation level and `READ ONLY` mode, if the transaction property `DEFERRABLE` is set, then that transaction executes with much lower overhead and is never canceled because of a serialization failure. This can be used for batch or long-running jobs, which need a consistent snapshot of the database without interfering or being interfered with by other transactions. For example:

```plpgsql
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE READ ONLY DEFERRABLE;
SELECT * FROM very_large_table;
COMMIT;
```

### Transaction priority

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

This ensures that the priority assigned to your transaction is in the range `[0.9-1.0]`.

## Learn more

- [Transaction isolation levels](../../../architecture/transactions/isolation-levels/) - Various isolation levels supported by YugabyteDB.
- [Concurrency control](../../../architecture/transactions/concurrency-control/) - Policies to handle conflicts between transactions.
- [Transaction priorities](../../../architecture/transactions/concurrency-control/) - Priority buckets for transactions.
- [Transaction options](../../../explore/transactions/distributed-transactions-ysql/#transaction-options) - Options supported by transactions.
