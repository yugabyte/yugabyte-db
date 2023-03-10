---
title: Architect highly availabile applications
headerTitle: Architecting High Availability
linkTitle: 10. High Availability
description: Learn how to architect Highly Available applications in YugabyteDB YSQL.
menu:
  preview:
    identifier: architect-high-availability
    parent: learn
    weight: 591
type: docs
---

## Ensuring HA of transactions

As failures are inevitable, design your applications to take appropriate actions on the failed statements to ensure they are highly available. YugabyteDB returns various error codes for errors that occur during transaction processing. YugabyteDB supports different levels of isolation during a transaction. Although some errors are very specific to certain isolation levels, most errors are common across multiple isolation levels.

The following scenarios describe the causes for failures, and the required methods to be handled by the applications.

## Types of Error Codes

Return codes typically fall into the following three categories:

1. __`WARNING`__ : Informational messages that explain why a statement failed.

     Most client libraries hide warnings but you might notice the messages when you execute statements directly from a terminal. The statement execution can continue without interruption but would need to be modified to avoid the re-occurence of the message as described in the following example:

    ```output.plpgsql
    -- When a BEGIN statement is issued inside a transaction.
    WARNING:  25001: there is already a transaction in progress
    ```

1. __`ERROR`__ : Errors are returned when a transaction cannot continue further and has to be restarted by the client.

    These errors need to be handled by the application and take appropriate action.

    ```output.plpgsql
    -- When multiple transactions are modifying the same key.
    ERROR:  40001: Operation expired: Transaction XXXX expired or aborted by a conflict
    ```

    For more details, refer to [Transaction retries](#transaction-retries).

1. __`FATAL`__ : Fatal messages are returned to notify that the connection to a server has been disconnected. At this point, the application should reconnect to the server. For example,

    ```output.plpgsql
    -- When the application takes a long time to issue a statement in the middle of a transaction
    FATAL:  25P03: terminating connection due to idle-in-transaction timeout
    ```

## Setup

Consider the following table format with some data for the following scenarios:

```sql
CREATE TABLE txndemo (
  k int,
  V int,
  PRIMARY KEY(k)
);
```

```sql
INSERT INTO txndemo VALUES (1,10),(2,10),(3,10),(4,10),(5,10);
```

## Transaction Retries

Consider the scenarios where deadlocks can be restarted for most errors that occur due to conflicts.

### 25P02 - InFailedSqlTransaction

This error occurs when a statement is issued after there's already an error in a transaction. The error message would be similar to the following:

```output.plpgsql
ERROR:  25P02: current transaction is aborted, commands ignored until end of transaction block
```

The only valid statements at this point would be `ROLLBACK` or `COMMIT`. The correct way to handle this is to resolve the actual error and then issue a rollback.

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

### 25P03 - Idle Timeout

When an application takes a long time between two statements in a transaction, it might hit the `idle_in_transaction_session_timeout` timeout. Set this timeout to avoid deadlock scenarios where applications acquire locks and then hang unintentionally. After the timeout is reached, the connection is disconnected and the client would have to reconnect. A typical error message would be as follows:

```output
FATAL:  25P03: terminating connection due to idle-in-transaction timeout
```

### 40001 - SerializationFailure

Serialization Failure happens when multiple transactions are updating the same set of keys (conflict), or when transactions are waiting for each other (deadlock). The error messages could be one of the following types:

- During a conflict, certain transactions are retried. However, after the retry limit is reached, an error occurs as follows:

    ```output.plpgsql
    ERROR:  40001: All transparent retries exhausted.
    ```

- All transactions are given a dynamic priority. When a deadlock is detected, the transaction with lower priority is automatically killed. For this scenario, the client might receive a message similar to the following:

    ```output.plpgsql
    ERROR:  40001: Operation expired: Heartbeat: Transaction XXXX expired or aborted by a conflict
    ```

    The correct way to handle this error is with a retry loop with exponential backoff as follows:

    ```python
    connstr = 'postgresql://yugabyte@localhost:5433/yugabyte'
    cxn = psycopg2.connect(connstr)

    max_attempts = 10   # max no.of retries
    sleep_time = 0.002  # 2 ms - base sleep time
    backoff = 2

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

## Non-Retriable Errors

Although most transactions can be retried on most erroneous scenarios, there are cases where retrying a transaction will not resolve an issue. For example, errors can occur when statements are issued out of place. These statements have to be fixed in code to continue further.

### Error - 25001

Transaction level isolation should be specified before the first statement of the transaction is executed. If not the following error occurs:

```output
yugabyte@yugabyte=# BEGIN;
BEGIN
Time: 0.797 ms
yugabyte@yugabyte=# UPDATE txndemo SET v=20 WHERE k=1;
UPDATE 0
Time: 10.416 ms
yugabyte@yugabyte=# SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
ERROR:  25001: SET TRANSACTION ISOLATION LEVEL must be called before any query
Time: 3.808 ms
```

### Error - 25006

This error occurs when a row is modified after specifying a transaction to be `READ ONLY` as follows:

```output
yugabyte@yugabyte=# BEGIN READ ONLY;
BEGIN
Time: 1.095 ms
yugabyte@yugabyte=# UPDATE txndemo SET v=20 WHERE k=1;
ERROR:  25006: cannot execute UPDATE in a read-only transaction
Time: 4.417 ms

```

## Special Cases

### Read Committed Isolation Level

In [Read Committed isolation level](../../../architecture/transactions/read-committed/), clients do not need to retry or handle serialization errors. During conflicts, the server retries indefinitely based on the [retry options](../../../architecture/transactions/read-committed/#performance-tuning) and [Wait-On-Conflict](../../../architecture/transactions/concurrency-control/#wait-on-conflict) policy.

To avoid getting stuck in a wait loop because of starvation, it is recommended to use a reasonable timeout for statements similar to the following:

```sql
SET statement_timeout = '10s';
```

### Deferrable Property

When a transaction is in `SERIALIZABLE` isolation level and `READ ONLY` mode, if the transaction property `DEFERRABLE` is set, then that transaction executes with much lower overhead and is never canceled because of a serialization failure. This property can be used for batch or long-running jobs.
