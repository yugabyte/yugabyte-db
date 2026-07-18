---
title: Transaction Error Codes in YSQL
headerTitle: Transaction error codes in YSQL
linkTitle: Error codes
description: Understand the error codes returned during transactions.
aliases:
  - /stable/develop/learn/acid-transactions/error-codes
menu:
  stable_develop:
    identifier: transaction-errorcodes-ysql
    parent: acid-transactions-1-ysql
    weight: 570
type: docs
---

Due to the strong [ACID](../../../../architecture/transactions/transactions-overview/) properties guaranteed by YugabyteDB, failures during transactions are inevitable. You need to design your applications to take appropriate action on failed statements to ensure they are highly available. YugabyteDB returns various error codes for errors that occur during transaction processing.

The following error codes typically occur during transaction processing.

## 25001: Active SQL transaction

This error occurs when certain statements that should be run outside a transaction block, typically because they have non-rollback-able side effects or do internal commits, are executed inside a transaction block. For example, issuing a `BEGIN` statement inside a transaction.

```output
WARNING:  25001: there is already a transaction in progress
```

{{<note>}}
**25001** errors are just warnings. But the code needs to be fixed to avoid future warnings.
{{</note>}}

## 25006: Read only SQL transaction

This error occurs when certain statements are executed in a read-only transaction that violate the read-only constraint. For example, modifying records inside a read-only transaction.

```output
ERROR:  25006: cannot execute UPDATE in a read-only transaction
```

{{<note>}}
**25006** errors are [Non-retriable](../transactions-retries-ysql/#non-retriable-errors). Writes should be removed from the read-only transaction code.
{{</note>}}

## 25P01: No active SQL transaction

This error occurs when certain statements that should be executed in a transaction are executed outside of a transaction. For example, issuing a `ROLLBACK` outside a transaction.

```output
WARNING:  25P01: there is no transaction in progress
```

{{<note>}}
**25P01** errors are just warnings. But the code needs to be fixed to avoid future warnings.
{{</note>}}

## 25P02: In failed SQL transaction

This error occurs when statements have failed inside a transaction and another statement other than `COMMIT` or `ROLLBACK` is executed.

```output
ERROR:  25P02: current transaction is aborted, commands ignored until end of transaction block
```

{{<note>}}
**25P02** errors are [Non-retriable](../transactions-retries-ysql/#non-retriable-errors). Proper error handling via a `try..catch` block and either `COMMIT` or `ROLLBACK` should be executed appropriately.
{{</note>}}

## 25P03: Idle in transaction session timeout

This occurs when an application stays idle longer than `idle_in_transaction_session_timeout` in the middle of a transaction.

```output
FATAL:  25P03: terminating connection due to idle-in-transaction timeout
```

{{<note>}}
The client can reconnect to the server and retry the transaction.
{{</note>}}

## 2D000: Invalid transaction termination

This error occurs when a transaction is terminated either by a `COMMIT` or a `ROLLBACK` in an invalid location. For example, when a `COMMIT` is issued inside a stored procedure that is called from inside a transaction.

```output
ERROR:  2D000: invalid transaction termination
```

{{<note>}}
**2D000** errors are [Non-retriable](../transactions-retries-ysql/#non-retriable-errors). The transaction code needs to be fixed to get around this error.
{{</note>}}

## 3B001: Invalid savepoint specification

This error occurs when you try to `ROLLBACK` to, or `RELEASE` a savepoint that has not been defined.

```output
ERROR:  3B001: savepoint "FIRST_SAVE" does not exist
```

{{<note>}}
**3B001** errors are [Non-retriable](../transactions-retries-ysql/#non-retriable-errors). The transaction code needs to be fixed to specify the correct savepoint name to fix this error.
{{</note>}}

## 40001: Serialization failure

This error occurs when a transaction cannot be applied or progress further because of other conflicting transactions. For example, when multiple transactions are modifying the same key.

```output
ERROR:  could not serialize access due to concurrent update (...)
```

{{<lead link="../transactions-retries-ysql/#client-side-retry">}}
Serialization failure errors can be retried by the client. See [Client-side retry](../transactions-retries-ysql/#client-side-retry).
{{</lead>}}

## 40P01: Deadlock detected

This error occurs when two or more transactions wait on each other to form a deadlock cycle. One or more of the transactions in the cycle are aborted and they fail with the following error.

```output
ERROR:  deadlock detected (...)
```

{{<lead link="../transactions-retries-ysql/#client-side-retry">}}
Deadlock detected errors can be retried by the client. See [Client-side retry](../transactions-retries-ysql/#client-side-retry).
{{</lead>}}

## 42XXX - Syntax Error or Access Rule Violation

Error codes starting with 42 typically relate to issues with SQL syntax, invalid references, or access permissions.

{{<warning>}}
Retrying these errors will likely have no effect unless the respective issue is fixed.
{{</warning>}}

Some of the errors are described in the following table:

| Code  |                                          Issue                                          |
| ----- | --------------------------------------------------------------------------------------- |
| 42000 | Syntax Error or Access Rule Violation (general class)                                   |
| 42601 | Syntax Error (invalid or unexpected SQL syntax)                                         |
| 42501 | Insufficient Privilege (lack of necessary permissions to perform the operation)         |
| 42846 | Cannot Coerce (incompatible data types in an operation or query)                        |
| 42883 | Undefined Function (referencing a function that doesn't exist or is incorrectly called) |
| 42P01 | Undefined Table (trying to reference a table that doesn't exist)                        |
| 42P02 | Undefined Parameter (using a parameter that has not been defined)                       |
| 42P03 | Duplicate Cursor (declaring a cursor that already exists)                               |
| 42703 | Undefined Column (referencing a column that doesn't exist in the table)                 |
| 42P04 | Duplicate Database (attempting to create a database that already exists)                |
| 42P05 | Duplicate Prepared Statement (trying to prepare a statement that already exists)        |
| 42P06 | Duplicate Schema (attempting to create a schema that already exists)                    |
| 42P07 | Duplicate Table (attempting to create a table that already exists)                      |
| 42P08 | Ambiguous Parameter (parameter is ambiguous in context)                                 |
| 42P09 | Ambiguous Alias (alias is ambiguous, referring to multiple possible options)            |
| 42P10 | Invalid Column Reference (column reference is incorrect or inappropriate)               |
| 42P11 | Invalid Cursor Definition (cursor declaration is invalid)                               |
| 42P12 | Invalid Database Definition (invalid database creation or operation)                    |
| 42P13 | Invalid Function Definition (improper definition of a function)                         |

## Learn more

- [Transaction isolation levels](../../../../architecture/transactions/isolation-levels/) - Various isolation levels that are supported by YugabyteDB.
- [Concurrency control](../../../../architecture/transactions/concurrency-control/) - Policies to handle conflicts between transactions.
- [Transaction priorities](../../../../architecture/transactions/transaction-priorities/) - Priority buckets for transactions.
- [Transaction options](../../../../explore/transactions/distributed-transactions-ysql/#transaction-options) - Options supported by transactions.
