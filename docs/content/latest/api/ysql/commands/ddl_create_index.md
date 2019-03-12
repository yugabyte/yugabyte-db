---
title: CREATE INDEX
linkTitle: CREATE INDEX
summary: Create index on a table in a database
description: CREATE INDEX
menu:
  latest:
    identifier: api-ysql-commands-create-index
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/ddl_create_index
isTocNested: true
showAsideToc: true
---

## Synopsis

## Syntax

### Diagrams

#### create_index
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="571" height="269" viewbox="0 0 571 269"><path class="connector" d="M0 21h5m67 0h30m67 0h20m-102 0q5 0 5 5v8q0 5 5 5h77q5 0 5-5v-8q0-5 5-5m5 0h10m56 0h50m30 0h10m45 0h10m61 0h20m-191 0q5 0 5 5v8q0 5 5 5h166q5 0 5-5v-8q0-5 5-5m5 0h10m55 0h20m-296 0q5 0 5 5v23q0 5 5 5h271q5 0 5-5v-23q0-5 5-5m5 0h5m-571 93h5m38 0h30m51 0h20m-86 0q5 0 5 5v8q0 5 5 5h61q5 0 5-5v-8q0-5 5-5m5 0h10m93 0h10m25 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h37m24 0h38q5 0 5 5v19q0 5-5 5m-5 0h30m25 0h5m-461 78h25m72 0h10m25 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h46m24 0h46q5 0 5 5v19q0 5-5 5m-5 0h30m25 0h20m-333 0q5 0 5 5v8q0 5 5 5h308q5 0 5-5v-8q0-5 5-5m5 0h5m-348 49h25m64 0h10m80 0h20m-189 0q5 0 5 5v8q0 5 5 5h164q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="67" height="24" rx="7"/><text class="text" x="15" y="21">CREATE</text><rect class="literal" x="102" y="5" width="67" height="24" rx="7"/><text class="text" x="112" y="21">UNIQUE</text><rect class="literal" x="199" y="5" width="56" height="24" rx="7"/><text class="text" x="209" y="21">INDEX</text><rect class="literal" x="305" y="5" width="30" height="24" rx="7"/><text class="text" x="315" y="21">IF</text><rect class="literal" x="345" y="5" width="45" height="24" rx="7"/><text class="text" x="355" y="21">NOT</text><rect class="literal" x="400" y="5" width="61" height="24" rx="7"/><text class="text" x="410" y="21">EXISTS</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="491" y="5" width="55" height="24"/><text class="text" x="501" y="21">name</text></a><rect class="literal" x="5" y="98" width="38" height="24" rx="7"/><text class="text" x="15" y="114">ON</text><rect class="literal" x="73" y="98" width="51" height="24" rx="7"/><text class="text" x="83" y="114">ONLY</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="154" y="98" width="93" height="24"/><text class="text" x="164" y="114">table_name</text></a><rect class="literal" x="257" y="98" width="25" height="24" rx="7"/><text class="text" x="267" y="114">(</text><rect class="literal" x="344" y="69" width="24" height="24" rx="7"/><text class="text" x="354" y="85">,</text><a xlink:href="../grammar_diagrams#index-elem"><rect class="rule" x="312" y="98" width="89" height="24"/><text class="text" x="322" y="114">index_elem</text></a><rect class="literal" x="431" y="98" width="25" height="24" rx="7"/><text class="text" x="441" y="114">)</text><rect class="literal" x="25" y="176" width="72" height="24" rx="7"/><text class="text" x="35" y="192">INCLUDE</text><rect class="literal" x="107" y="176" width="25" height="24" rx="7"/><text class="text" x="117" y="192">(</text><rect class="literal" x="203" y="147" width="24" height="24" rx="7"/><text class="text" x="213" y="163">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="162" y="176" width="106" height="24"/><text class="text" x="172" y="192">column_name</text></a><rect class="literal" x="298" y="176" width="25" height="24" rx="7"/><text class="text" x="308" y="192">)</text><rect class="literal" x="25" y="225" width="64" height="24" rx="7"/><text class="text" x="35" y="241">WHERE</text><a xlink:href="../grammar_diagrams#predicate"><rect class="rule" x="99" y="225" width="80" height="24"/><text class="text" x="109" y="241">predicate</text></a></svg>

#### index_elem
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="429" height="68" viewbox="0 0 429 68"><path class="connector" d="M0 21h25m106 0h72m-193 0q5 0 5 5v19q0 5 5 5h5m25 0h10m88 0h10m25 0h5q5 0 5-5v-19q0-5 5-5m5 0h30m68 0h20m-103 0q5 0 5 5v8q0 5 5 5h78q5 0 5-5v-8q0-5 5-5m5 0h30m44 0h29m-83 24q0 5 5 5h5m53 0h5q5 0 5-5m-78-24q5 0 5 5v32q0 5 5 5h63q5 0 5-5v-32q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="5" width="106" height="24"/><text class="text" x="35" y="21">column_name</text></a><rect class="literal" x="25" y="34" width="25" height="24" rx="7"/><text class="text" x="35" y="50">(</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="60" y="34" width="88" height="24"/><text class="text" x="70" y="50">expression</text></a><rect class="literal" x="158" y="34" width="25" height="24" rx="7"/><text class="text" x="168" y="50">)</text><a xlink:href="../grammar_diagrams#opclass"><rect class="rule" x="233" y="5" width="68" height="24"/><text class="text" x="243" y="21">opclass</text></a><rect class="literal" x="351" y="5" width="44" height="24" rx="7"/><text class="text" x="361" y="21">ASC</text><rect class="literal" x="351" y="34" width="53" height="24" rx="7"/><text class="text" x="361" y="50">DESC</text></svg>

### Grammar
```
create_index ::= CREATE [ UNIQUE ] INDEX [ [ IF NOT EXISTS ] name ]
                 ON [ ONLY ] table_name ( index_elem [ , ...] )
    [ INCLUDE ( column_name [, ...] ) ]
    [ WHERE predicate ]

index_elem ::= { column_name | '(' expression ')' } [ opclass ] [ 'ASC' | 'DESC' ] ;
```

Where
- `UNIQUE` enforced that duplicate values in a table in not allowed.

- `INCLUDE clause` specifies a list of columns which will be included in the index as non-key columns.

- `name` specifies the index to be created.

- `table_name` specifies the name of the table to be indexed.

- `column_name` specifies the name of a column of the table.

- expression specifies one or more columns of the table and must be surrounded by parentheses.

- `ASC` indicates ascending sort order.

- DESC indicates descending sort order.

## Semantics

- `CONCURRENTLY`, `USING method`, `COLLATE`, `NULL order`, and `tablespace` options are not yet supported.

## See Also
[Other YSQL Statements](..)
