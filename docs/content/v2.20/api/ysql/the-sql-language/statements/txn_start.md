---
title: START TRANSACTION statement [YSQL]
headerTitle: START TRANSACTION
linkTitle: START TRANSACTION
description: Use the `START TRANSACTION` statement to start a transaction with the default (or specified) isolation level.
menu:
  v2.20:
    identifier: txn_start
    parent: statements
type: docs
---

## Synopsis

Use the `START TRANSACTION` statement to start a transaction with the default (or specified) isolation level.

## Syntax

{{%ebnf%}}
  start_transaction
{{%/ebnf%}}

## Semantics

The `START TRANSACTION` statement is simply an alternative spelling for the [`BEGIN`](../txn_begin) statement. The syntax that follows `START TRANSACTION` is identical to that syntax that follows `BEGIN [ TRANSACTION | WORK ]`. And the two alternative spellings have identical semantics.

## See also

- [`ABORT`](../txn_abort)
- [`BEGIN`](../txn_begin)
- [`COMMIT`](../txn_commit)
- [`END`](../txn_end)
- [`ROLLBACK`](../txn_rollback)
- [`SET TRANSACTION`](../txn_set)
