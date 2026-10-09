---
title: LOCK statement [YSQL]
headerTitle: LOCK
linkTitle: LOCK
description: Use the LOCK statement to lock a table.
menu:
  stable_api:
    identifier: txn_lock
    parent: statements
aliases:
  - /stable/api/ysql/commands/txn_lock/
type: docs
---

## Synopsis

Use the `LOCK` statement to lock a table.

## Syntax

{{%ebnf%}}
  lock_table,
  lockmode
{{%/ebnf%}}

{{< note title="Table inheritance" >}}
`ONLY` and `*` in [table_expr](../../../syntax_resources/grammar_diagrams/#table-expr) apply when other tables inherit a listed table. See [Table inheritance](../../ddl-inherit/).
{{< /note >}}

## Semantics

### *lock_table*

#### *name*

Specify a table to lock.

### *lockmode*

- Only `ACCESS SHARE` lock mode is supported at this time.
- All other modes listed in *lockmode* are under development.

```
ACCESS SHARE
  | ROW SHARE
  | ROW EXCLUSIVE
  | SHARE UPDATE EXCLUSIVE
  | SHARE
  | SHARE ROW EXCLUSIVE
  | EXCLUSIVE
  | ACCESS EXCLUSIVE
```

## See also

- [SET TRANSACTION](../txn_set)
