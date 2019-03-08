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

### Diagram 

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="1551" height="93" viewbox="0 0 1551 93"><path class="connector" d="M0 50h5m67 0h30m67 0h20m-102 0q5 0 5 5v8q0 5 5 5h77q5 0 5-5v-8q0-5 5-5m5 0h10m56 0h50m30 0h10m45 0h10m61 0h20m-191 0q5 0 5 5v8q0 5 5 5h166q5 0 5-5v-8q0-5 5-5m5 0h10m55 0h20m-296 0q5 0 5 5v23q0 5 5 5h271q5 0 5-5v-23q0-5 5-5m5 0h10m38 0h30m51 0h20m-86 0q5 0 5 5v8q0 5 5 5h61q5 0 5-5v-8q0-5 5-5m5 0h10m93 0h10m25 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h37m24 0h38q5 0 5 5v19q0 5-5 5m-5 0h30m25 0h30m72 0h10m25 0h10m113 0h10m25 0h20m-300 0q5 0 5 5v8q0 5 5 5h275q5 0 5-5v-8q0-5 5-5m5 0h30m64 0h10m80 0h20m-189 0q5 0 5 5v8q0 5 5 5h164q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="34" width="67" height="24" rx="7"/><text class="text" x="15" y="50">CREATE</text><rect class="literal" x="102" y="34" width="67" height="24" rx="7"/><text class="text" x="112" y="50">UNIQUE</text><rect class="literal" x="199" y="34" width="56" height="24" rx="7"/><text class="text" x="209" y="50">INDEX</text><rect class="literal" x="305" y="34" width="30" height="24" rx="7"/><text class="text" x="315" y="50">IF</text><rect class="literal" x="345" y="34" width="45" height="24" rx="7"/><text class="text" x="355" y="50">NOT</text><rect class="literal" x="400" y="34" width="61" height="24" rx="7"/><text class="text" x="410" y="50">EXISTS</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="491" y="34" width="55" height="24"/><text class="text" x="501" y="50">name</text></a><rect class="literal" x="576" y="34" width="38" height="24" rx="7"/><text class="text" x="586" y="50">ON</text><rect class="literal" x="644" y="34" width="51" height="24" rx="7"/><text class="text" x="654" y="50">ONLY</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="725" y="34" width="93" height="24"/><text class="text" x="735" y="50">table_name</text></a><rect class="literal" x="828" y="34" width="25" height="24" rx="7"/><text class="text" x="838" y="50">(</text><rect class="literal" x="915" y="5" width="24" height="24" rx="7"/><text class="text" x="925" y="21">,</text><a xlink:href="../grammar_diagrams#index-elem"><rect class="rule" x="883" y="34" width="89" height="24"/><text class="text" x="893" y="50">index_elem</text></a><rect class="literal" x="1002" y="34" width="25" height="24" rx="7"/><text class="text" x="1012" y="50">)</text><rect class="literal" x="1057" y="34" width="72" height="24" rx="7"/><text class="text" x="1067" y="50">INCLUDE</text><rect class="literal" x="1139" y="34" width="25" height="24" rx="7"/><text class="text" x="1149" y="50">(</text><a xlink:href="../grammar_diagrams#column-names"><rect class="rule" x="1174" y="34" width="113" height="24"/><text class="text" x="1184" y="50">column_names</text></a><rect class="literal" x="1297" y="34" width="25" height="24" rx="7"/><text class="text" x="1307" y="50">)</text><rect class="literal" x="1372" y="34" width="64" height="24" rx="7"/><text class="text" x="1382" y="50">WHERE</text><a xlink:href="../grammar_diagrams#predicate"><rect class="rule" x="1446" y="34" width="80" height="24"/><text class="text" x="1456" y="50">predicate</text></a></svg>

### Grammar
```
create_index ::= CREATE [ UNIQUE ] INDEX [ [ IF NOT EXISTS ] name ] ON [ ONLY ] table_name
    ( { column_name | ( expression ) } [ opclass ] [ ASC | DESC ] [, ...] )
    [ INCLUDE ( column_name [, ...] ) ]
    [ WITH ( storage_parameter = value [, ... ] ) ]
    [ WHERE predicate ]
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
[Other PostgreSQL Statements](..)
