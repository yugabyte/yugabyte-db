---
title: ALTER TABLE
linkTitle: ALTER TABLE
summary: Alter a table in a database
description: ALTER TABLE
menu:
  latest:
    identifier: api-ysql-commands-alter-table
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/ddl_alter_table
isTocNested: true
showAsideToc: true
---

## Synopsis
`ALTER TABLE` changes or redefines one or more attributes of a table.

## Syntax

### Diagrams

#### alter_table
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="558" height="78" viewbox="0 0 558 78"><path class="connector" d="M0 50h5m57 0h10m57 0h30m51 0h20m-86 0q5 0 5 5v8q0 5 5 5h61q5 0 5-5v-8q0-5 5-5m5 0h10m55 0h30m26 0h20m-61 0q5 0 5 5v8q0 5 5 5h36q5 0 5-5v-8q0-5 5-5m5 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h59m24 0h59q5 0 5 5v19q0 5-5 5m-5 0h25"/><rect class="literal" x="5" y="34" width="57" height="24" rx="7"/><text class="text" x="15" y="50">ALTER</text><rect class="literal" x="72" y="34" width="57" height="24" rx="7"/><text class="text" x="82" y="50">TABLE</text><rect class="literal" x="159" y="34" width="51" height="24" rx="7"/><text class="text" x="169" y="50">ONLY</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="240" y="34" width="55" height="24"/><text class="text" x="250" y="50">name</text></a><rect class="literal" x="325" y="34" width="26" height="24" rx="7"/><text class="text" x="335" y="50">*</text><rect class="literal" x="455" y="5" width="24" height="24" rx="7"/><text class="text" x="465" y="21">,</text><a xlink:href="../grammar_diagrams#alter-table-action"><rect class="rule" x="401" y="34" width="132" height="24"/><text class="text" x="411" y="50">alter_table_action</text></a></svg>

#### alter_table_action
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="520" height="277" viewbox="0 0 520 277"><path class="connector" d="M0 21h25m46 0h30m71 0h20m-106 0q5 0 5 5v8q0 5 5 5h81q5 0 5-5v-8q0-5 5-5m5 0h10m106 0h10m82 0h115m-500 39q0 5 5 5h5m71 0h10m36 0h10m93 0h255q5 0 5-5m-490 29q0 5 5 5h5m54 0h30m71 0h20m-106 0q5 0 5 5v8q0 5 5 5h81q5 0 5-5v-8q0-5 5-5m5 0h10m106 0h30m77 0h20m-107 24q0 5 5 5h5m77 0h5q5 0 5-5m-102-24q5 0 5 5v32q0 5 5 5h87q5 0 5-5v-32q0-5 5-5m5 0h57q5 0 5-5m-490 63q0 5 5 5h5m46 0h10m122 0h297q5 0 5-5m-490 29q0 5 5 5h5m54 0h10m96 0h10m125 0h30m77 0h20m-107 24q0 5 5 5h5m77 0h5q5 0 5-5m-102-24q5 0 5 5v32q0 5 5 5h87q5 0 5-5v-32q0-5 5-5m5 0h53q5 0 5-5m-495-160q5 0 5 5v218q0 5 5 5h5m71 0h30m71 0h20m-106 0q5 0 5 5v8q0 5 5 5h81q5 0 5-5v-8q0-5 5-5m5 0h10m106 0h10m36 0h10m106 0h5q5 0 5-5v-218q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="46" height="24" rx="7"/><text class="text" x="35" y="21">ADD</text><rect class="literal" x="101" y="5" width="71" height="24" rx="7"/><text class="text" x="111" y="21">COLUMN</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="202" y="5" width="106" height="24"/><text class="text" x="212" y="21">column_name</text></a><a xlink:href="../grammar_diagrams#data-type"><rect class="rule" x="318" y="5" width="82" height="24"/><text class="text" x="328" y="21">data_type</text></a><rect class="literal" x="25" y="49" width="71" height="24" rx="7"/><text class="text" x="35" y="65">RENAME</text><rect class="literal" x="106" y="49" width="36" height="24" rx="7"/><text class="text" x="116" y="65">TO</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="152" y="49" width="93" height="24"/><text class="text" x="162" y="65">table_name</text></a><rect class="literal" x="25" y="78" width="54" height="24" rx="7"/><text class="text" x="35" y="94">DROP</text><rect class="literal" x="109" y="78" width="71" height="24" rx="7"/><text class="text" x="119" y="94">COLUMN</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="210" y="78" width="106" height="24"/><text class="text" x="220" y="94">column_name</text></a><rect class="literal" x="346" y="78" width="77" height="24" rx="7"/><text class="text" x="356" y="94">RESTRICT</text><rect class="literal" x="346" y="107" width="77" height="24" rx="7"/><text class="text" x="356" y="123">CASCADE</text><rect class="literal" x="25" y="141" width="46" height="24" rx="7"/><text class="text" x="35" y="157">ADD</text><a xlink:href="../grammar_diagrams#table-constraint"><rect class="rule" x="81" y="141" width="122" height="24"/><text class="text" x="91" y="157">table_constraint</text></a><rect class="literal" x="25" y="170" width="54" height="24" rx="7"/><text class="text" x="35" y="186">DROP</text><rect class="literal" x="89" y="170" width="96" height="24" rx="7"/><text class="text" x="99" y="186">CONSTRAINT</text><a xlink:href="../grammar_diagrams#constraint-name"><rect class="rule" x="195" y="170" width="125" height="24"/><text class="text" x="205" y="186">constraint_name</text></a><rect class="literal" x="350" y="170" width="77" height="24" rx="7"/><text class="text" x="360" y="186">RESTRICT</text><rect class="literal" x="350" y="199" width="77" height="24" rx="7"/><text class="text" x="360" y="215">CASCADE</text><rect class="literal" x="25" y="233" width="71" height="24" rx="7"/><text class="text" x="35" y="249">RENAME</text><rect class="literal" x="126" y="233" width="71" height="24" rx="7"/><text class="text" x="136" y="249">COLUMN</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="227" y="233" width="106" height="24"/><text class="text" x="237" y="249">column_name</text></a><rect class="literal" x="343" y="233" width="36" height="24" rx="7"/><text class="text" x="353" y="249">TO</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="389" y="233" width="106" height="24"/><text class="text" x="399" y="249">column_name</text></a></svg>

#### table_constraint
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="684" height="49" viewbox="0 0 684 49"><path class="connector" d="M0 21h25m96 0h10m125 0h20m-266 0q5 0 5 5v8q0 5 5 5h241q5 0 5-5v-8q0-5 5-5m5 0h10m60 0h10m25 0h10m88 0h10m25 0h30m38 0h10m67 0h20m-150 0q5 0 5 5v8q0 5 5 5h125q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="96" height="24" rx="7"/><text class="text" x="35" y="21">CONSTRAINT</text><a xlink:href="../grammar_diagrams#constraint-name"><rect class="rule" x="131" y="5" width="125" height="24"/><text class="text" x="141" y="21">constraint_name</text></a><rect class="literal" x="286" y="5" width="60" height="24" rx="7"/><text class="text" x="296" y="21">CHECK</text><rect class="literal" x="356" y="5" width="25" height="24" rx="7"/><text class="text" x="366" y="21">(</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="391" y="5" width="88" height="24"/><text class="text" x="401" y="21">expression</text></a><rect class="literal" x="489" y="5" width="25" height="24" rx="7"/><text class="text" x="499" y="21">)</text><rect class="literal" x="544" y="5" width="38" height="24" rx="7"/><text class="text" x="554" y="21">NO</text><rect class="literal" x="592" y="5" width="67" height="24" rx="7"/><text class="text" x="602" y="21">INHERIT</text></svg>

#### column_names
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="156" height="63" viewbox="0 0 156 63"><path class="connector" d="M0 50h25m-5 0q-5 0-5-5v-19q0-5 5-5h46m24 0h46q5 0 5 5v19q0 5-5 5m-5 0h25"/><rect class="literal" x="66" y="5" width="24" height="24" rx="7"/><text class="text" x="76" y="21">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="34" width="106" height="24"/><text class="text" x="35" y="50">column_name</text></a></svg>

### Grammar
```
alter_table ::= ALTER TABLE [ ONLY ] name [ * ] alter_table_action [, ...] ;

alter_table_action ::=
  { ADD [ COLUMN ] column_name data_type
    | RENAME TO table_name
    | DROP [ COLUMN ] column_name [ RESTRICT | CASCADE ]
    | ADD table_constraint
    | DROP CONSTRAINT constraint_name [ RESTRICT | CASCADE ]
    | RENAME [ COLUMN ] column_name TO column_name } ;

table_constraint ::= [ CONSTRAINT constraint_name ] CHECK ( expression ) [ NO INHERIT ] ;
```

## Semantics

- An error is raised if specified table does not exist.
- `ADD COLUMN` adds new column.
- `DROP COLUMN` drops existing column.
- `ADD table_constraint` adds new table_constraint.
- `DROP table_constraint` drops existing table_constraint.
- Other `ALTER TABLE` options are not yet supported.

## See Also
[`CREATE TABLE`](../ddl_create_table)
[`DROP TABLE`](../ddl_drop_table)
[Other YSQL Statements](..)
