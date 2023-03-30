---
title: Transaction Error Codes in YSQL
headerTitle: Transaction error codes in YSQL
linkTitle: Error codes
description: Understand the error codes returned during a transactions.
aliases:
  - /preview/develop/learn/acid-transactions/error-codes
menu:
  preview:
    identifier: transaction-errorcodes-ysql
    parent: acid-transactions-1-ysql
    weight: 568
type: docs
---

Due to the strong [ACID](../../../../architecture/transactions/transactions-overview/#acid-properties) properties guaranteed by YugabyteDB, failures during transactions are inevitable. You need to design your applications to take appropriate action on failed statements to ensure they are highly available. The following error codes typically occur during transaction processing.

## 25001: Active SQL transaction

This error occurs when certain statements that should be run outside a transaction block, typically because they have non-rollback-able side effects or do internal commits, are executed inside a transaction block. For example, attempting to [create a database](../../../../api/ysql/the-sql-language/statements/ddl_create_database/) inside a transaction would result in the following error:

```output
ERROR:  25001: CREATE DATABASE cannot run inside a transaction block
```

Issuing a [BEGIN](../../../../api/ysql/the-sql-language/statements/txn_begin) statement inside a transaction would result in:

```output
WARNING:  25001: there is already a transaction in progress
```

## 25006: Read only SQL transaction

This error occurs when certain statements are executed in a read only transaction that violate the read only constraint. For example, modifying records inside a read-only transaction.

```output
ERROR:  25006: cannot execute UPDATE in a read-only transaction
```

## 25P01: No active SQL Transaction

This error occurs when certain statements that should be executed in a transaction are executed outside of a transaction. For example, issuing a [ROLLBACK](../../../../api/ysql/the-sql-language/statements/txn_rollback) outside a transaction.

```output
WARNING:  25P01: there is no transaction in progress
```

## 25P02: In failed SQL transaction

This error occurs when statements have failed inside a transaction and another statement other than [COMMIT](../../../../api/ysql/the-sql-language/statements/txn_commit) or [ROLLBACK](../../../../api/ysql/the-sql-language/statements/txn_rollback) is executed.

```output
ERROR:  25P02: current transaction is aborted, commands ignored until end of transaction block
```

## 25P03: Idle in transaction session timeout

This occurs when an application stays idle longer than `idle_in_transaction_session_timeout` in the middle of a transaction.

```output
FATAL:  25P03: terminating connection due to idle-in-transaction timeout
```

## 40001: Serialization failure

This error occurs when a transaction cannot be applied or progress further because of other conflicting transactions. For example, when multiple transactions are modifying the same key.

```output
ERROR:  40001: Operation expired: Transaction XXXX expired or aborted by a conflict
```

```output
ERROR:  40001: Operation failed. Try again: XXXX Conflicts with higher priority transaction: YYYY
```

## 2D000: Invalid transaction termination

This error occurs when a transaction is terminated either by a [COMMIT](../../../../api/ysql/the-sql-language/statements/txn_commit) or a [ROLLBACK](../../../../api/ysql/the-sql-language/statements/txn_rollback) in an invalid location. For example, when a `COMMIT` is issued inside a stored procedure that is called from inside a transaction.

```output
ERROR:  2D000: invalid transaction termination
```

## 3B001: Invalid savepoint specification

This error occurs when you try to [ROLLBACK](../../../../api/ysql/the-sql-language/statements/txn_rollback) to or [RELEASE](../../../../api/ysql/the-sql-language/statements/savepoint_release) a savepoint that has not been defined.

```output
ERROR:  3B001: savepoint "FIRST_SAVE" does not exist
```

## Learn more

- [Transaction isolation levels](../../../../architecture/transactions/isolation-levels/) - Various isolation levels supported by YugabyteDB.
- [Concurrency control](../../../../architecture/transactions/concurrency-control/) - Policies to handle conflicts between transactions.
- [Transaction priorities](../../../../architecture/transactions/transaction-priorities/) - Priority buckets for transactions.
- [Transaction options](../../../../explore/transactions/distributed-transactions-ysql/#transaction-options) - Options supported by transactions.
