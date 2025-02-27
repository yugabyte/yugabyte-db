---
title: SHOW TRANSACTION statement [YSQL]
headerTitle: SHOW TRANSACTION
linkTitle: SHOW TRANSACTION
description: Use the SHOW TRANSACTION ISOLATION LEVEL statement to show the current transaction isolation level.
summary: SHOW TRANSACTION
menu:
  stable_api:
    identifier: txn_show
    parent: statements
type: docs
---

## Synopsis

Use the `SHOW TRANSACTION ISOLATION LEVEL` statement to show the current transaction isolation level.

## Syntax

{{%ebnf%}}
  show_transaction
{{%/ebnf%}}

## Semantics

Supports Serializable, Snapshot, and Read Committed Isolation using the PostgreSQL isolation level syntax of `SERIALIZABLE`, `REPEATABLE READ`, and `READ COMMITTED` respectively. PostgreSQL's `READ UNCOMMITTED` also maps to Read Committed Isolation.

### TRANSACTION ISOLATION LEVEL

Show the current transaction isolation level.

The `TRANSACTION ISOLATION LEVEL` returned is either `SERIALIZABLE`, `REPEATABLE READ`, `READ COMMITTED`, or `READ UNCOMMITTED`.

## See also

- [`SET TRANSACTION`](../txn_set)
- [`Transaction isolation levels`](../../../../../architecture/transactions/isolation-levels)
