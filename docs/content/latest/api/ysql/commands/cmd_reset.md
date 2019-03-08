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
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="174" height="63" viewbox="0 0 174 63"><path class="connector" d="M0 21h5m59 0h30m55 0h20m-90 0q5 0 5 5v19q0 5 5 5h5m40 0h20q5 0 5-5v-19q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="59" height="24" rx="7"/><text class="text" x="15" y="21">RESET</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="94" y="5" width="55" height="24"/><text class="text" x="104" y="21">name</text></a><rect class="literal" x="94" y="34" width="40" height="24" rx="7"/><text class="text" x="104" y="50">ALL</text></svg>

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
