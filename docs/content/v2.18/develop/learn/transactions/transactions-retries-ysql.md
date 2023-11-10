---
title: Transaction retries in YSQL
headerTitle: Transaction retries in YSQL
linkTitle: Transaction retries
description: Learn how to retry transactions in YSQL.
menu:
  stable:
    identifier: transactions-retries-ysql
    parent: acid-transactions-1-ysql
    weight: 567
type: docs
---

YugabyteDB returns different [error codes](../transactions-errorcodes-ysql/) for the various scenarios that go wrong during transaction processing. Applications need to be designed to handle these scenarios correctly to be highly available so that users aren't impacted. Although most errors are common across multiple isolation levels, some errors are very specific to certain [transaction isolation levels](../../../../explore/transactions/isolation-levels/).

The examples in the following sections illustrate failure scenarios and techniques you can use to handle these failures in your applications.

## Prerequisites

Follow the [setup instructions](../../../../explore#tabs-00-00) to start a single local YugabytedDB instance, and create a table as follows:

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

## Automatic retries

YugabyteDB retries failed transactions automatically on the server side whenever possible without client intervention as per the [concurrency control policies](../../../../architecture/transactions/concurrency-control/#best-effort-internal-retries-for-first-statement-in-a-transaction). This is the case even for single statements, which are implicitly considered transactions. In [Read Committed](../../../../explore/transactions/isolation-levels/#read-committed-isolation) isolation mode, the server retries indefinitely.

In some scenarios, a server-side retry is not suitable. For example, the retry limit has been reached or the transaction is not in a valid state. In these cases, it is the client's responsibility to retry the transaction at the application layer.

## Client-side retry

Most transaction errors that happen due to conflicts and deadlocks can be restarted by the client. The following scenarios describe the causes for failures, and the required methods to be handled by the applications.

Execute the transaction in a `try..catch` block in a loop. When a re-tryable failure happens, issue [ROLLBACK](../../../../api/ysql/the-sql-language/statements/txn_rollback/) and then retry the transaction. To avoid overloading the server and ending up in an indefinite loop, wait for some time between retries and limit the number of retries. The following illustrates a typical client-side retry implementation:

```python
max_attempts = 10   # max no.of retries
sleep_time = 0.002  # 2 ms - base sleep time
backoff = 2         # exponential multiplier

attempt = 0
while attempt < max_attempts:
    attempt += 1
    try :
        cursor = cxn.cursor()
        cursor.execute("BEGIN");

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

##### 40001 - SerializationFailure

SerializationFailure errors happen when multiple transactions are updating the same set of keys (conflict) or when transactions are waiting on each other (deadlock). The error messages could be one of the following types:

- During a conflict, certain transactions are retried. However, after the retry limit is reached, an error occurs as follows:

    ```output
    ERROR:  40001: All transparent retries exhausted.
    ```

- All transactions are given a dynamic priority. When a deadlock is detected, the transaction with lower priority is automatically killed. For this scenario, the client might receive a message similar to the following:

    ```output
    ERROR:  40001: Operation expired: Heartbeat: Transaction XXXX expired or aborted by a conflict
    ```

The correct way to handle this error is with a retry loop with exponential backoff, as described in [Client-side retry](#client-side-retry). When the [UPDATE](../../../../api/ysql/the-sql-language/statements/dml_update/) or [COMMIT](../../../../api/ysql/the-sql-language/statements/txn_commit/) fails because of `SerializationFailure`, the code retries after waiting for `sleep_time` seconds, up to `max_attempts`.

{{<tip title="Read Committed">}}
In read committed isolation level, as the server retries internally, the client does not need to worry about handling SerializationFailure. Only transactions operating in repeated read and serializable levels need to handle serialization failures.
{{</tip>}}

Another way to handle these failures is would be to rollback to a checkpoint before the failed statement and proceed further as described in [Savepoints](#savepoints).

## Savepoints

[Savepoints](../../../../api/ysql/the-sql-language/statements/savepoint_create/) are named checkpoints that can be used to rollback just a few statements, and then proceed with the transaction, rather than aborting the entire transaction when there is an error.

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

If the row `[k=1]` already exists in the table, the [INSERT](../../../../api/ysql/the-sql-language/statements/dml_insert/) would result in a UniqueViolation exception. Technically, the transaction would be in an error state and further statements would result in a [25P02: In failed SQL transaction](#25p02-infailedsqltransaction) error. You have to catch the exception and rollback. But instead of rolling back the entire transaction, you can rollback to the previously declared savepoint `before_insert`, and update the value of the row with `k=1`. Then you can continue with other statements in the transaction.

## Non-retriable errors

Although most transactions can be retried in most error scenarios, there are cases where retrying a transaction will not resolve an issue. For example, errors can occur when statements are issued out of place. These statements have to be fixed in code to continue further.

##### 25001 - Specify transaction isolation level

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

##### 25006 - Modify a row in a read-only transaction

This error occurs when a row is modified after specifying a transaction to be [READ ONLY](../../../../api/ysql/the-sql-language/statements/txn_set/#read-only-mode) as follows:

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

##### 25P02 - InFailedSqlTransaction

This error occurs when a statement is issued after there's already an error in a transaction. The error message would be similar to the following:

```output
ERROR:  25P02: current transaction is aborted, commands ignored until end of transaction block
```

Consider the following scenario:

```plpgsql
BEGIN;
```

```output
BEGIN
Time: 0.393 ms
```

```plpgsql
INVALID TXN STATEMENT;
```

```output
ERROR:  42601: syntax error at or near "INVALID"
Time: 2.523 ms
```

```plpgsql
SELECT * from txndemo where k=1;
```

```output
ERROR:  25P02: current transaction is aborted, commands ignored until end of transaction block
Time: 17.074 ms
```

The only valid statements at this point would be [ROLLBACK](../../../../api/ysql/the-sql-language/statements/txn_rollback/) or [COMMIT](../../../../api/ysql/the-sql-language/statements/txn_commit/).

## Learn more

- [Transaction isolation levels](../../../../architecture/transactions/isolation-levels/) - Various isolation levels supported by YugabyteDB.
- [Concurrency control](../../../../architecture/transactions/concurrency-control/) - Policies to handle conflicts between transactions.
- [Transaction priorities](../../../../architecture/transactions/transaction-priorities/) - Priority buckets for transactions.
- [Transaction options](../../../../explore/transactions/distributed-transactions-ysql/#transaction-options) - Options supported by transactions.
