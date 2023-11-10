---
title: LOCK statement [YSQL]
headerTitle: LOCK
linkTitle: LOCK
description: Use the LOCK statement to lock a table.
menu:
  v2.18:
    identifier: txn_lock
    parent: statements
type: docs
---

## Synopsis

Use the `LOCK` statement to lock a table.

## Syntax

{{%ebnf%}}
  lock_table,
  lockmode
{{%/ebnf%}}

{{< note title="Table inheritance is not yet supported" >}}
The [table_expr](../../../syntax_resources/grammar_diagrams/#table-expr) rule specifies syntax that is useful only when at least one other table inherits one of the tables that the `truncate` statement lists explicitly. See [this note](../ddl_alter_table#table-expr-note) for more detail. Until inheritance is supported, use a bare [table_name](../../../syntax_resources/grammar_diagrams/#table-name).
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

- [`SET TRANSACTION`](../txn_set)
