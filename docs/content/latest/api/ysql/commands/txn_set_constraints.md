---
title: SET CONSTRAINTS
linkTitle: SET CONSTRAINTS
summary: SET CONSTRAINTS
description: SET CONSTRAINTS
menu:
  latest:
    identifier: api-ysql-commands-set-constraints
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/cmd_set_constraints
isTocNested: true
showAsideToc: true
---

## Synopsis

`SET CONSTRAINTS` command checks timing for current transaction.

## Syntax

### Diagram 

### Grammar
```
set_constraints ::= SET CONSTRAINTS { ALL | name [, ...] } { DEFERRED | IMMEDIATE }
```

## Semantics

- Attributes in `SET CONSTRAINTS` does not apply to `NOT NULL` and `CHECK` constraints.

## See Also
[Other PostgreSQL Statements](..)
