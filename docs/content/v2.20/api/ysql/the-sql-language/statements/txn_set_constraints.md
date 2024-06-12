---
title: SET CONSTRAINTS statement [YSQL]
headerTitle: SET CONSTRAINTS
linkTitle: SET CONSTRAINTS
summary: SET CONSTRAINTS
description: Use the `SET CONSTRAINTS` statement to set the timing of constraint checking within the current transaction.
menu:
  v2.20:
    identifier: txn_set_constraints
    parent: statements
type: docs
---

## Synopsis

Use the `SET CONSTRAINTS` statement to set the timing of constraint checking within the current transaction.

## Syntax

{{%ebnf%}}
  set_constraints
{{%/ebnf%}}

## Semantics

Attributes in the `SET CONSTRAINTS` statement comply with the behavior defined in the SQL standard, except that it does not apply to `NOT NULL` and `CHECK` constraints.

### *set_constraints*

```
SET CONSTRAINTS { ALL | *name [ , ... ] } { DEFERRED | IMMEDIATE }
```

### ALL

Change the mode of all deferrable constraints.

### *name*

Specify one or a list of constraint names.

### DEFERRED

Set constraints to not be checked until transaction commit.

Uniqueness and exclusion constraints are checked immediately, unless marked `DEFERRABLE`.

### IMMEDIATE

Set constraints to take effect retroactively.

See also

- [`ALTER TABLE`](../ddl_alter_table)
