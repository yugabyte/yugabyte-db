---
title: LOCK
linkTitle: LOCK
summary: Lock a table
description: LOCK
menu:
  latest:
    identifier: api-ysql-commands-lock
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/txn_lock
isTocNested: true
showAsideToc: true
---

## Synopsis

`LOCK` command locks a table.

## Syntax

### Diagrams

#### lock
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="804" height="78" viewbox="0 0 804 78"><path class="connector" d="M0 50h5m50 0h30m57 0h20m-92 0q5 0 5 5v8q0 5 5 5h67q5 0 5-5v-8q0-5 5-5m5 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h109m24 0h109q5 0 5 5v19q0 5-5 5m-237 0h20m51 0h20m-86 0q5 0 5 5v8q0 5 5 5h61q5 0 5-5v-8q0-5 5-5m5 0h10m55 0h30m26 0h20m-61 0q5 0 5 5v8q0 5 5 5h36q5 0 5-5v-8q0-5 5-5m5 0h50m32 0h10m80 0h10m56 0h20m-223 0q5 0 5 5v8q0 5 5 5h198q5 0 5-5v-8q0-5 5-5m5 0h30m67 0h20m-102 0q5 0 5 5v8q0 5 5 5h77q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="34" width="50" height="24" rx="7"/><text class="text" x="15" y="50">LOCK</text><rect class="literal" x="85" y="34" width="57" height="24" rx="7"/><text class="text" x="95" y="50">TABLE</text><rect class="literal" x="296" y="5" width="24" height="24" rx="7"/><text class="text" x="306" y="21">,</text><rect class="literal" x="212" y="34" width="51" height="24" rx="7"/><text class="text" x="222" y="50">ONLY</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="293" y="34" width="55" height="24"/><text class="text" x="303" y="50">name</text></a><rect class="literal" x="378" y="34" width="26" height="24" rx="7"/><text class="text" x="388" y="50">*</text><rect class="literal" x="474" y="34" width="32" height="24" rx="7"/><text class="text" x="484" y="50">IN</text><a xlink:href="../grammar_diagrams#lockmode"><rect class="rule" x="516" y="34" width="80" height="24"/><text class="text" x="526" y="50">lockmode</text></a><rect class="literal" x="606" y="34" width="56" height="24" rx="7"/><text class="text" x="616" y="50">MODE</text><rect class="literal" x="712" y="34" width="67" height="24" rx="7"/><text class="text" x="722" y="50">NOWAIT</text></svg>

#### lockmode

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="285" height="237" viewbox="0 0 285 237"><path class="connector" d="M0 21h25m68 0h10m61 0h116m-265 24q0 5 5 5h5m48 0h10m61 0h121q5 0 5-5m-255 29q0 5 5 5h5m48 0h10m85 0h97q5 0 5-5m-255 29q0 5 5 5h5m61 0h10m69 0h10m85 0h5q5 0 5-5m-255 29q0 5 5 5h5m61 0h179q5 0 5-5m-255 29q0 5 5 5h5m61 0h10m48 0h10m85 0h26q5 0 5-5m-255 29q0 5 5 5h5m85 0h155q5 0 5-5m-260-169q5 0 5 5v193q0 5 5 5h5m68 0h10m85 0h77q5 0 5-5v-193q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="68" height="24" rx="7"/><text class="text" x="35" y="21">ACCESS</text><rect class="literal" x="103" y="5" width="61" height="24" rx="7"/><text class="text" x="113" y="21">SHARE</text><rect class="literal" x="25" y="34" width="48" height="24" rx="7"/><text class="text" x="35" y="50">ROW</text><rect class="literal" x="83" y="34" width="61" height="24" rx="7"/><text class="text" x="93" y="50">SHARE</text><rect class="literal" x="25" y="63" width="48" height="24" rx="7"/><text class="text" x="35" y="79">ROW</text><rect class="literal" x="83" y="63" width="85" height="24" rx="7"/><text class="text" x="93" y="79">EXCLUSIVE</text><rect class="literal" x="25" y="92" width="61" height="24" rx="7"/><text class="text" x="35" y="108">SHARE</text><rect class="literal" x="96" y="92" width="69" height="24" rx="7"/><text class="text" x="106" y="108">UPDATE</text><rect class="literal" x="175" y="92" width="85" height="24" rx="7"/><text class="text" x="185" y="108">EXCLUSIVE</text><rect class="literal" x="25" y="121" width="61" height="24" rx="7"/><text class="text" x="35" y="137">SHARE</text><rect class="literal" x="25" y="150" width="61" height="24" rx="7"/><text class="text" x="35" y="166">SHARE</text><rect class="literal" x="96" y="150" width="48" height="24" rx="7"/><text class="text" x="106" y="166">ROW</text><rect class="literal" x="154" y="150" width="85" height="24" rx="7"/><text class="text" x="164" y="166">EXCLUSIVE</text><rect class="literal" x="25" y="179" width="85" height="24" rx="7"/><text class="text" x="35" y="195">EXCLUSIVE</text><rect class="literal" x="25" y="208" width="68" height="24" rx="7"/><text class="text" x="35" y="224">ACCESS</text><rect class="literal" x="103" y="208" width="85" height="24" rx="7"/><text class="text" x="113" y="224">EXCLUSIVE</text></svg>

### Grammar
```
lock_table ::= LOCK [ TABLE ] [ ONLY ] name [ * ] [, ...] [ IN lockmode MODE ] [ NOWAIT ]

lockmode ::= { ACCESS SHARE |
               ROW SHARE |
               ROW EXCLUSIVE |
               SHARE UPDATE EXCLUSIVE |
               SHARE |
               SHARE ROW EXCLUSIVE |
               EXCLUSIVE |
               ACCESS EXCLUSIVE }
```

Where
- name specifies an existing table to be locked.

## Semantics

- Only `ACCESS SHARE` lock mode is supported at this time.
- All other modes listed in `lockmode` are under development.

## See Also
[`CREATE TABLE`](../ddl_create_table)
[Other PostgreSQL Statements](..)
