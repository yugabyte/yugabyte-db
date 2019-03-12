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

### Diagrams

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="448" height="92" viewbox="0 0 448 92"><path class="connector" d="M0 21h5m43 0h10m104 0h30m40 0h75m-130 0q5 0 5 5v48q0 5 5 5h25m-5 0q-5 0-5-5v-19q0-5 5-5h20m24 0h21q5 0 5 5v19q0 5-5 5m-5 0h25q5 0 5-5v-48q0-5 5-5m5 0h30m85 0h21m-121 0q5 0 5 5v19q0 5 5 5h5m86 0h5q5 0 5-5v-19q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="43" height="24" rx="7"/><text class="text" x="15" y="21">SET</text><rect class="literal" x="58" y="5" width="104" height="24" rx="7"/><text class="text" x="68" y="21">CONSTRAINTS</text><rect class="literal" x="192" y="5" width="40" height="24" rx="7"/><text class="text" x="202" y="21">ALL</text><rect class="literal" x="227" y="34" width="24" height="24" rx="7"/><text class="text" x="237" y="50">,</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="212" y="63" width="55" height="24"/><text class="text" x="222" y="79">name</text></a><rect class="literal" x="337" y="5" width="85" height="24" rx="7"/><text class="text" x="347" y="21">DEFERRED</text><rect class="literal" x="337" y="34" width="86" height="24" rx="7"/><text class="text" x="347" y="50">IMMEDIATE</text></svg>

### Grammar
```
set_constraints ::= SET CONSTRAINTS { ALL | name [, ...] } { DEFERRED | IMMEDIATE }
```

## Semantics

- Attributes in `SET CONSTRAINTS` does not apply to `NOT NULL` and `CHECK` constraints.

## See Also
[Other YSQL Statements](..)
