---
title: SHOW
linkTitle: SHOW
summary: Display values of a system or session variable
description: SHOW
menu:
  latest:
    identifier: api-ysql-commands-show
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/cmd_show
isTocNested: true
showAsideToc: true
---

## Synopsis

## Syntax

### Diagram 

### Grammar
```
show_stmt ::= SHOW { name | ALL }
```

Where
- `name` specifies the name of the parameter to be showed.

## Semantics

- Although the values of a parameter can be set, showed, and reset, the effect of these parameters are not yet supported in YugaByte. The factory-settings or default behaviors will be used for the moment.

## See Also
[`SET`](../cmd_set)
[`RESET`](../cmd_reset)
[Other PostgreSQL Statements](..)
