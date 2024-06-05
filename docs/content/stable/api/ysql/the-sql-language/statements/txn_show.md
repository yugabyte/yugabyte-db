---
title: SHOW TRANSACTION statement [YSQL]
headerTitle: SHOW TRANSACTION
linkTitle: SHOW TRANSACTION
description: Use the SHOW TRANSACTION ISOLATION LEVEL statement to show the current transaction isolation level.
summary: SHOW TRANSACTION
menu:
  stable:
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

Supports Serializable, Snapshot, and Read Committed {{<badge/ea>}} Isolation using the PostgreSQL isolation level syntax of `SERIALIZABLE`, `REPEATABLE READ`, and `READ COMMITTED` respectively. PostgreSQL's `READ UNCOMMITTED` also maps to Read Committed Isolation.

Read Committed Isolation is supported only if the YB-TServer flag `yb_enable_read_committed_isolation` is set to `true`. By default this flag is `false` and in this case the Read Committed isolation level of YugabyteDB's transactional layer falls back to the stricter Snapshot Isolation (in which case `READ COMMITTED` and `READ UNCOMMITTED` of YSQL also in turn use Snapshot Isolation).

### TRANSACTION ISOLATION LEVEL

Show the current transaction isolation level.

The `TRANSACTION ISOLATION LEVEL` returned is either `SERIALIZABLE`, `REPEATABLE READ`, `READ COMMITTED` or `READ UNCOMMITTED`.

## See also

- [`SET TRANSACTION`](../txn_set)
- [`Transaction isolation levels`](../../../../../architecture/transactions/isolation-levels)
