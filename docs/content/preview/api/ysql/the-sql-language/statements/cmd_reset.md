---
title: RESET statement [YSQL]
headerTitle: RESET
linkTitle: RESET
description: Use the RESET statement to restore the value of a run-time parameter to the default value.
aliases:
  - /preview/api/ysql/the-sql-language/cmd_reset
menu:
  preview_api:
    identifier: cmd_reset
    parent: statements
aliases:
  - /preview/api/ysql/commands/cmd_reset/
type: docs
---

## Synopsis

Use the `RESET` statement to restore the value of a run-time parameter to the default value. `RESET` is an alternative spelling for `SET run_time_parameter TO DEFAULT`.

## Syntax

{{%ebnf%}}
  reset_stmt
{{%/ebnf%}}

## Semantics

### *run_time_parameter*

Specify the name of a mutable run-time parameter.

## See also

- [`SHOW`](../cmd_show)
- [`SET`](../cmd_set)
