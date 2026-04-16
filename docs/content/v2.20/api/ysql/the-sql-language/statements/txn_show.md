---
title: SHOW TRANSACTION statement [YSQL]
headerTitle: SHOW TRANSACTION
linkTitle: SHOW TRANSACTION
description: Use the SHOW TRANSACTION ISOLATION LEVEL statement to show the current transaction isolation level.
summary: SHOW TRANSACTION
menu:
  v2.20_api:
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

Supports Serializable, Snapshot, and Read Committed isolation using the PostgreSQL isolation level syntax of `SERIALIZABLE`, `REPEATABLE READ`, and `READ COMMITTED` respectively. PostgreSQL's `READ UNCOMMITTED` also maps to Read Committed isolation.

Read Committed isolation {{<tags/feature/ea idea="1099">}} is supported only if the YB-TServer flag `yb_enable_read_committed_isolation` is set to `true`. By default this flag is `false` and the Read Committed isolation level of the YugabyteDB transactional layer falls back to the stricter Snapshot isolation (in which case Read Committed and Read Uncommitted of YSQL also in turn use Snapshot isolation).

### TRANSACTION ISOLATION LEVEL

Show the current transaction isolation level.

The `TRANSACTION ISOLATION LEVEL` returned is either `SERIALIZABLE`, `REPEATABLE READ`, `READ COMMITTED` or `READ UNCOMMITTED`.

## See also

- [`SET TRANSACTION`](../txn_set)
- [`Transaction isolation levels`](../../../../../architecture/transactions/isolation-levels)
