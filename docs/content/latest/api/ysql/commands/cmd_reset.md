---
title: RESET
linkTitle: RESET
summary: Reset a system or session variable to factory settings.
description: RESET
menu:
  latest:
    identifier: api-ysql-commands-reset
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/cmd_reset
isTocNested: true
showAsideToc: true
---

## Synopsis

`RESET` command sets the value of a parameter to the default value.

## Syntax

### Diagram 

### Grammar
```
reset_stmt := RESET { name | ALL }
```

Where

- name specifies the name of a mutable run-time parameter

## Semantics

- Although the values of a parameter can be set, showed, and reset, the effect of these parameters are not yet supported in YugaByte. The factory-settings or default behaviors will be used for the moment.

## See Also
[`SHOW`](../cmd_show)
[`SET`](../cmd_set)
[Other PostgreSQL Statements](..)
