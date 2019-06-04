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
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="558" height="78" viewbox="0 0 558 78"><path class="connector" d="M0 50h5m57 0h10m57 0h30m51 0h20m-86 0q5 0 5 5v8q0 5 5 5h61q5 0 5-5v-8q0-5 5-5m5 0h10m55 0h30m26 0h20m-61 0q5 0 5 5v8q0 5 5 5h36q5 0 5-5v-8q0-5 5-5m5 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h59m24 0h59q5 0 5 5v19q0 5-5 5m-5 0h25"/><rect class="literal" x="5" y="34" width="57" height="24" rx="7"/><text class="text" x="15" y="50">ALTER</text><rect class="literal" x="72" y="34" width="57" height="24" rx="7"/><text class="text" x="82" y="50">TABLE</text><rect class="literal" x="159" y="34" width="51" height="24" rx="7"/><text class="text" x="169" y="50">ONLY</text><a xlink:href="../../grammar_diagrams#name"><rect class="rule" x="240" y="34" width="55" height="24"/><text class="text" x="250" y="50">name</text></a><rect class="literal" x="325" y="34" width="26" height="24" rx="7"/><text class="text" x="335" y="50">*</text><rect class="literal" x="455" y="5" width="24" height="24" rx="7"/><text class="text" x="465" y="21">,</text><a xlink:href="../../grammar_diagrams#alter-table-action"><rect class="rule" x="401" y="34" width="132" height="24"/><text class="text" x="411" y="50">alter_table_action</text></a></svg>

#### alter_table_action
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="521" height="285" viewbox="0 0 521 285"><path class="connector" d="M0 22h25m46 0h30m72 0h20m-107 0q5 0 5 5v8q0 5 5 5h82q5 0 5-5v-8q0-5 5-5m5 0h10m106 0h10m80 0h117m-501 40q0 5 5 5h5m71 0h10m36 0h10m91 0h258q5 0 5-5m-491 30q0 5 5 5h5m53 0h30m72 0h20m-107 0q5 0 5 5v8q0 5 5 5h82q5 0 5-5v-8q0-5 5-5m5 0h10m106 0h30m79 0h20m-109 25q0 5 5 5h5m77 0h7q5 0 5-5m-104-25q5 0 5 5v33q0 5 5 5h89q5 0 5-5v-33q0-5 5-5m5 0h56q5 0 5-5m-491 65q0 5 5 5h5m46 0h10m152 0h268q5 0 5-5m-491 30q0 5 5 5h5m53 0h10m98 0h10m122 0h30m79 0h20m-109 25q0 5 5 5h5m77 0h7q5 0 5-5m-104-25q5 0 5 5v33q0 5 5 5h89q5 0 5-5v-33q0-5 5-5m5 0h54q5 0 5-5m-496-165q5 0 5 5v225q0 5 5 5h5m71 0h30m72 0h20m-107 0q5 0 5 5v8q0 5 5 5h82q5 0 5-5v-8q0-5 5-5m5 0h10m106 0h10m36 0h10m106 0h5q5 0 5-5v-225q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="46" height="25" rx="7"/><text class="text" x="35" y="22">ADD</text><rect class="literal" x="101" y="5" width="72" height="25" rx="7"/><text class="text" x="111" y="22">COLUMN</text><a xlink:href="../../grammar_diagrams#column-name"><rect class="rule" x="203" y="5" width="106" height="25"/><text class="text" x="213" y="22">column_name</text></a><a xlink:href="../../grammar_diagrams#data-type"><rect class="rule" x="319" y="5" width="80" height="25"/><text class="text" x="329" y="22">data_type</text></a><rect class="literal" x="25" y="50" width="71" height="25" rx="7"/><text class="text" x="35" y="67">RENAME</text><rect class="literal" x="106" y="50" width="36" height="25" rx="7"/><text class="text" x="116" y="67">TO</text><a xlink:href="../../grammar_diagrams#table-name"><rect class="rule" x="152" y="50" width="91" height="25"/><text class="text" x="162" y="67">table_name</text></a><rect class="literal" x="25" y="80" width="53" height="25" rx="7"/><text class="text" x="35" y="97">DROP</text><rect class="literal" x="108" y="80" width="72" height="25" rx="7"/><text class="text" x="118" y="97">COLUMN</text><a xlink:href="../../grammar_diagrams#column-name"><rect class="rule" x="210" y="80" width="106" height="25"/><text class="text" x="220" y="97">column_name</text></a><rect class="literal" x="346" y="80" width="79" height="25" rx="7"/><text class="text" x="356" y="97">RESTRICT</text><rect class="literal" x="346" y="110" width="77" height="25" rx="7"/><text class="text" x="356" y="127">CASCADE</text><rect class="literal" x="25" y="145" width="46" height="25" rx="7"/><text class="text" x="35" y="162">ADD</text><a xlink:href="../../grammar_diagrams#alter-table-constraint"><rect class="rule" x="81" y="145" width="152" height="25"/><text class="text" x="91" y="162">alter_table_constraint</text></a><rect class="literal" x="25" y="175" width="53" height="25" rx="7"/><text class="text" x="35" y="192">DROP</text><rect class="literal" x="88" y="175" width="98" height="25" rx="7"/><text class="text" x="98" y="192">CONSTRAINT</text><a xlink:href="../../grammar_diagrams#constraint-name"><rect class="rule" x="196" y="175" width="122" height="25"/><text class="text" x="206" y="192">constraint_name</text></a><rect class="literal" x="348" y="175" width="79" height="25" rx="7"/><text class="text" x="358" y="192">RESTRICT</text><rect class="literal" x="348" y="205" width="77" height="25" rx="7"/><text class="text" x="358" y="222">CASCADE</text><rect class="literal" x="25" y="240" width="71" height="25" rx="7"/><text class="text" x="35" y="257">RENAME</text><rect class="literal" x="126" y="240" width="72" height="25" rx="7"/><text class="text" x="136" y="257">COLUMN</text><a xlink:href="../../grammar_diagrams#column-name"><rect class="rule" x="228" y="240" width="106" height="25"/><text class="text" x="238" y="257">column_name</text></a><rect class="literal" x="344" y="240" width="36" height="25" rx="7"/><text class="text" x="354" y="257">TO</text><a xlink:href="../../grammar_diagrams#column-name"><rect class="rule" x="390" y="240" width="106" height="25"/><text class="text" x="400" y="257">column_name</text></a></svg>

#### alter_table_constraint
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="787" height="65" viewbox="0 0 787 65"><path class="connector" d="M0 22h25m98 0h10m122 0h20m-265 0q5 0 5 5v8q0 5 5 5h240q5 0 5-5v-8q0-5 5-5m5 0h30m61 0h10m25 0h10m83 0h10m25 0h253m-492 0q5 0 5 5v20q0 5 5 5h5m75 0h10m43 0h10m25 0h10m112 0h10m25 0h10m127 0h5q5 0 5-5v-20q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="98" height="25" rx="7"/><text class="text" x="35" y="22">CONSTRAINT</text><a xlink:href="../../grammar_diagrams#constraint-name"><rect class="rule" x="133" y="5" width="122" height="25"/><text class="text" x="143" y="22">constraint_name</text></a><rect class="literal" x="305" y="5" width="61" height="25" rx="7"/><text class="text" x="315" y="22">CHECK</text><rect class="literal" x="376" y="5" width="25" height="25" rx="7"/><text class="text" x="386" y="22">(</text><a xlink:href="../../grammar_diagrams#expression"><rect class="rule" x="411" y="5" width="83" height="25"/><text class="text" x="421" y="22">expression</text></a><rect class="literal" x="504" y="5" width="25" height="25" rx="7"/><text class="text" x="514" y="22">)</text><rect class="literal" x="305" y="35" width="75" height="25" rx="7"/><text class="text" x="315" y="52">FOREIGN</text><rect class="literal" x="390" y="35" width="43" height="25" rx="7"/><text class="text" x="400" y="52">KEY</text><rect class="literal" x="443" y="35" width="25" height="25" rx="7"/><text class="text" x="453" y="52">(</text><a xlink:href="../../grammar_diagrams#column-names"><rect class="rule" x="478" y="35" width="112" height="25"/><text class="text" x="488" y="52">column_names</text></a><rect class="literal" x="600" y="35" width="25" height="25" rx="7"/><text class="text" x="610" y="52">)</text><a xlink:href="../../grammar_diagrams#references-clause"><rect class="rule" x="635" y="35" width="127" height="25"/><text class="text" x="645" y="52">references_clause</text></a></svg>

### Grammar
```
alter_table ::= ALTER TABLE [ ONLY ] name [ * ] alter_table_action [, ...]

alter_table_action ::=
  { ADD [ COLUMN ] column_name data_type
    | RENAME TO table_name
    | DROP [ COLUMN ] column_name [ RESTRICT | CASCADE ]
    | ADD alter_table_constraint
    | DROP CONSTRAINT constraint_name [ RESTRICT | CASCADE ]
    | RENAME [ COLUMN ] column_name TO column_name }

alter_table_constraint ::= [ CONSTRAINT constraint_name ]
                           { CHECK ( expression ) |
                            FOREIGN KEY ( column_names ) references_clause }
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
