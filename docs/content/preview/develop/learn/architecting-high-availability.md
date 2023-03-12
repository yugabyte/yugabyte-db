---
title: Architect highly available applications
headerTitle: High availability
linkTitle: 10. High availability
description: Learn how to architect Highly Available applications in YugabyteDB YSQL.
headcontent: Build applications to handle failures and remain highly available
menu:
  preview:
    identifier: architect-high-availability
    parent: learn
    weight: 591
type: docs
---

Although failures are inevitable, you can design your applications to take appropriate action when statements fail to ensure they are highly available.

YugabyteDB returns [standard PostgreSQL error codes](https://www.postgresql.org/docs/11/errcodes-appendix.html) for errors that occur during transaction processing. In general, the error codes can be classified into the following three types:

1. __`WARNING`__ : Informational messages that explain why a statement failed.

    ```output.plpgsql
    -- When a BEGIN statement is issued inside a transaction
    WARNING:  25001: there is already a transaction in progress
    ```

    Most client libraries hide warnings but you might notice the messages when you execute statements directly from a terminal. Statement execution can continue without interruption, but would need to be modified to avoid the re-occurence of the message.

1. __`ERROR`__ : Errors are returned when a transaction cannot continue and has to be restarted by the client.

    ```output.plpgsql
    -- When multiple transactions are modifying the same key.
    ERROR:  40001: Operation expired: Transaction XXXX expired or aborted by a conflict
    ```

    These errors need to be handled by the application to take appropriate action.

1. __`FATAL`__ : Fatal messages are returned to notify that the connection to a server has been disconnected.

    ```output.plpgsql
    -- When the application takes a long time to issue a statement in the middle of a transaction.
    FATAL:  25P03: terminating connection due to idle-in-transaction timeout
    ```

    At this point, the application should reconnect to the server.

YugabyteDB also supports different [isolation levels](../../../explore/transactions/isolation-levels/) during a transaction. While some errors are specific to certain transaction isolation levels, most errors are common across multiple isolation levels.

The following examples illustrate failure scenarios and techniques you can use to handle these failures in your applications.

## Prerequisites

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

The server retries failed transactions automatically whenever possible. However, in some scenarios, a server-side retry is not possible. For example, when the retry limit has been reached or the transaction is not in a valid state. In these cases, it is the client's responsibility to retry the transaction at the application layer. Most transaction errors that happen due to conflicts and deadlocks can be restarted by the client. The following scenarios describe the causes of failures, and the required methods to be handled by the applications.

### Client-side retry

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

    ```output.plpgsql
    ERROR:  40001: All transparent retries exhausted.
    ```

- All transactions are given a dynamic priority. When a deadlock is detected, the transaction with lower priority is automatically killed. For this scenario, the client might receive a message similar to the following:

    ```output.plpgsql
    ERROR:  40001: Operation expired: Heartbeat: Transaction XXXX expired or aborted by a conflict
    ```

The correct way to handle this error is with a retry loop with exponential backoff, as described in [Client-side retry](#client-side-retry). For example:

```python
connstr = 'postgresql://yugabyte@localhost:5433/yugabyte'
cxn = psycopg2.connect(connstr)

max_attempts = 10   # max no.of retries
sleep_time = 0.002  # 2 ms - base sleep time
backoff = 2         # 

attempt = 0
while attempt < max_attempts:
  attempt += 1
  try :
    cursor = cxn.cursor()
    cursor.execute("BEGIN ISOLATION LEVEL SERIALIZABLE");
    cursor.execute("UPDATE txndemo set v=20 WHERE k=1;");
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

```output.plpgsql
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
  cursor.execute("UPDATE txndemo SET v=20 WHERE k=1;")

except psycopg2.errors.InFailedSqlTransaction as e:
  print(e)
  cursor.execute("ROLLBACK")
```

Normally the `INVALID TXN STATEMENT` would throw a `SyntaxError` exception. For the sake of this example, the code (incorrectly) catches the error and ignores it. The next `UPDATE` statement, even though valid, will fail with an `InFailedSqlTransaction` exception. This could be avoided by handling the actual error and issuing a `ROLLBACK`. In this case, a retry won't help because it is a SyntaxError, but there might be scenarios where the transaction would succeed when retried.

## Non-retriable errors

Although most transactions can be retried in most error scenarios, there are cases where retrying a transaction will not resolve an issue. For example, errors can occur when statements are issued out of place. These statements have to be fixed in code to continue further.

### Error - 25001

Transaction level isolation should be specified before the first statement of the transaction is executed. If not the following error occurs:

```plpgsql
BEGIN;
```

```output.plpgsql
BEGIN
Time: 0.797 ms
```

```plpgsql
UPDATE txndemo SET v=20 WHERE k=1;
```

```output.plpgsql
UPDATE 0
Time: 10.416 ms
```

```plpgsql
SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
```

```output.plpgsql
ERROR:  25001: SET TRANSACTION ISOLATION LEVEL must be called before any query
Time: 3.808 ms
```

### Error - 25006

This error occurs when a row is modified after specifying a transaction to be `READ ONLY` as follows:

```plpgsql
BEGIN READ ONLY;
```

```output.plpgsql
BEGIN
Time: 1.095 ms
```

```plpgsql
UPDATE txndemo SET v=20 WHERE k=1;
```

```output.plpgsql
ERROR:  25006: cannot execute UPDATE in a read-only transaction
Time: 4.417 ms
```

## Performance tuning

### Statement timeout

In [READ COMMITTED isolation level](../../../architecture/transactions/read-committed/), clients do not need to retry or handle serialization errors. During conflicts, the server retries indefinitely based on the [retry options](../../../architecture/transactions/read-committed/#performance-tuning) and [Wait-On-Conflict](../../../architecture/transactions/concurrency-control/#wait-on-conflict) policy.

To avoid getting stuck in a wait loop because of starvation, use a reasonable timeout for the statements similar to the following:

```plpgsql
SET statement_timeout = '10s';
```

This ensures that the transaction would not be blocked for more than 10 seconds.

### Idle timeout

When an application takes a long time between two statements in a transaction or just hangs, it could be still holding the locks on the [provisional records](../../../architecture/transactions/distributed-txns/#provisional-records) during that period. It would hit a timeout if the `idle_in_transaction_session_timeout` is set accordingly. After that timeout is reached, the connection is disconnected and the client would have to reconnect. The typical error message would be:

```output.plpgsql
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

```output.plpgsql
 idle_in_transaction_session_timeout
-------------------------------------
 10s
```

Setting this timeout can avoid deadlock scenarios where applications acquire locks and then hang unintentionally.

### Deferrable property

When a transaction is in `SERIALIZABLE` isolation level and `READ ONLY` mode, and the transaction property `DEFERRABLE` is set, then that transaction executes with much lower overhead and is never canceled because of a serialization failure. This can be used for batch or long-running jobs. For example:

```plpgsql
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE READ ONLY DEFERRABLE;
SELECT * FROM very_large_table;
COMMIT;
```

### Transaction priority

As noted, all transactions are dynamically assigned a priority. This is a value in the range of `[0.0, 1.0]`. The current priority can be fetched using the `yb_transaction_priority` setting as follows:

```plpgsql
show yb_transaction_priority;
```

```output.plpgsql
          yb_transaction_priority
-------------------------------------------
 0.000000000 (Normal priority transaction)
```

The priority value is bound by two settings, `yb_transaction_priority_lower_bound` and `yb_transaction_priority_upper_bound`. If an application would like a specific transaction to be given higher priority, it can issue statements as follows:

```plpgsql
SET yb_transaction_priority_lower_bound=0.9;
SET yb_transaction_priority_upper_bound=1.0;
```

This ensures that the priority assigned to your transaction is in the range `[0.9-1.0]`.

## Learn more

- [Transaction isolation levels](../../../architecture/transactions/isolation-levels/) - Various isolation levels supported by YugabyteDB.
- [Concurrency control](../../../architecture/transactions/concurrency-control/) - Policies to handle conflicts between transactions.
- [Transaction priorities](../../../architecture/transactions/transaction-priorities/) - Priority buckets for transactions.
- [Transaction options](../../../explore/transactions/distributed-transactions-ysql/#transaction-options) - options supported by transactions.