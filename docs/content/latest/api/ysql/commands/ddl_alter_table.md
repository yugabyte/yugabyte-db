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

### alter_table
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="744" height="78" viewbox="0 0 744 78"><path class="connector" d="M0 50h5m57 0h10m57 0h30m30 0h10m61 0h20m-136 0q5 0 5 5v8q0 5 5 5h111q5 0 5-5v-8q0-5 5-5m5 0h30m51 0h20m-86 0q5 0 5 5v8q0 5 5 5h61q5 0 5-5v-8q0-5 5-5m5 0h10m55 0h30m26 0h20m-61 0q5 0 5 5v8q0 5 5 5h36q5 0 5-5v-8q0-5 5-5m5 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h59m24 0h59q5 0 5 5v19q0 5-5 5m-5 0h30m25 0h5"/><rect class="literal" x="5" y="34" width="57" height="24" rx="7"/><text class="text" x="15" y="50">ALTER</text><rect class="literal" x="72" y="34" width="57" height="24" rx="7"/><text class="text" x="82" y="50">TABLE</text><rect class="literal" x="159" y="34" width="30" height="24" rx="7"/><text class="text" x="169" y="50">IF</text><rect class="literal" x="199" y="34" width="61" height="24" rx="7"/><text class="text" x="209" y="50">EXISTS</text><rect class="literal" x="310" y="34" width="51" height="24" rx="7"/><text class="text" x="320" y="50">ONLY</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="391" y="34" width="55" height="24"/><text class="text" x="401" y="50">name</text></a><rect class="literal" x="476" y="34" width="26" height="24" rx="7"/><text class="text" x="486" y="50">*</text><rect class="literal" x="606" y="5" width="24" height="24" rx="7"/><text class="text" x="616" y="21">,</text><a xlink:href="../grammar_diagrams#alter-table-action"><rect class="rule" x="552" y="34" width="132" height="24"/><text class="text" x="562" y="50">alter_table_action</text></a><a xlink:href="../grammar_diagrams#]"><rect class="rule" x="714" y="34" width="25" height="24"/><text class="text" x="724" y="50">]</text></a></svg>

#### alter_table_action
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="631" height="204" viewbox="0 0 631 204"><path class="connector" d="M0 21h25m46 0h30m71 0h20m-106 0q5 0 5 5v8q0 5 5 5h81q5 0 5-5v-8q0-5 5-5m5 0h30m30 0h10m45 0h10m61 0h20m-191 0q5 0 5 5v8q0 5 5 5h166q5 0 5-5v-8q0-5 5-5m5 0h10m106 0h10m82 0h20m-611 39q0 5 5 5h5m54 0h30m71 0h20m-106 0q5 0 5 5v8q0 5 5 5h81q5 0 5-5v-8q0-5 5-5m5 0h30m30 0h10m61 0h20m-136 0q5 0 5 5v8q0 5 5 5h111q5 0 5-5v-8q0-5 5-5m5 0h10m106 0h30m77 0h20m-107 24q0 5 5 5h5m77 0h5q5 0 5-5m-102-24q5 0 5 5v32q0 5 5 5h87q5 0 5-5v-32q0-5 5-5m5 0h17q5 0 5-5m-601 63q0 5 5 5h5m46 0h10m122 0h408q5 0 5-5m-606-102q5 0 5 5v126q0 5 5 5h5m54 0h10m96 0h30m30 0h10m61 0h20m-136 0q5 0 5 5v8q0 5 5 5h111q5 0 5-5v-8q0-5 5-5m5 0h10m125 0h30m77 0h20m-107 24q0 5 5 5h5m77 0h5q5 0 5-5m-102-24q5 0 5 5v32q0 5 5 5h87q5 0 5-5v-32q0-5 5-5m5 0h13q5 0 5-5v-126q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="46" height="24" rx="7"/><text class="text" x="35" y="21">ADD</text><rect class="literal" x="101" y="5" width="71" height="24" rx="7"/><text class="text" x="111" y="21">COLUMN</text><rect class="literal" x="222" y="5" width="30" height="24" rx="7"/><text class="text" x="232" y="21">IF</text><rect class="literal" x="262" y="5" width="45" height="24" rx="7"/><text class="text" x="272" y="21">NOT</text><rect class="literal" x="317" y="5" width="61" height="24" rx="7"/><text class="text" x="327" y="21">EXISTS</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="408" y="5" width="106" height="24"/><text class="text" x="418" y="21">column_name</text></a><a xlink:href="../grammar_diagrams#data-type"><rect class="rule" x="524" y="5" width="82" height="24"/><text class="text" x="534" y="21">data_type</text></a><rect class="literal" x="25" y="49" width="54" height="24" rx="7"/><text class="text" x="35" y="65">DROP</text><rect class="literal" x="109" y="49" width="71" height="24" rx="7"/><text class="text" x="119" y="65">COLUMN</text><rect class="literal" x="230" y="49" width="30" height="24" rx="7"/><text class="text" x="240" y="65">IF</text><rect class="literal" x="270" y="49" width="61" height="24" rx="7"/><text class="text" x="280" y="65">EXISTS</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="361" y="49" width="106" height="24"/><text class="text" x="371" y="65">column_name</text></a><rect class="literal" x="497" y="49" width="77" height="24" rx="7"/><text class="text" x="507" y="65">RESTRICT</text><rect class="literal" x="497" y="78" width="77" height="24" rx="7"/><text class="text" x="507" y="94">CASCADE</text><rect class="literal" x="25" y="112" width="46" height="24" rx="7"/><text class="text" x="35" y="128">ADD</text><a xlink:href="../grammar_diagrams#table-constraint"><rect class="rule" x="81" y="112" width="122" height="24"/><text class="text" x="91" y="128">table_constraint</text></a><rect class="literal" x="25" y="141" width="54" height="24" rx="7"/><text class="text" x="35" y="157">DROP</text><rect class="literal" x="89" y="141" width="96" height="24" rx="7"/><text class="text" x="99" y="157">CONSTRAINT</text><rect class="literal" x="215" y="141" width="30" height="24" rx="7"/><text class="text" x="225" y="157">IF</text><rect class="literal" x="255" y="141" width="61" height="24" rx="7"/><text class="text" x="265" y="157">EXISTS</text><a xlink:href="../grammar_diagrams#constraint-name"><rect class="rule" x="346" y="141" width="125" height="24"/><text class="text" x="356" y="157">constraint_name</text></a><rect class="literal" x="501" y="141" width="77" height="24" rx="7"/><text class="text" x="511" y="157">RESTRICT</text><rect class="literal" x="501" y="170" width="77" height="24" rx="7"/><text class="text" x="511" y="186">CASCADE</text></svg>

#### table_constraint
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="790" height="78" viewbox="0 0 790 78"><path class="connector" d="M0 21h25m96 0h10m125 0h20m-266 0q5 0 5 5v8q0 5 5 5h241q5 0 5-5v-8q0-5 5-5m5 0h30m60 0h10m25 0h10m88 0h10m25 0h30m38 0h10m67 0h20m-150 0q5 0 5 5v8q0 5 5 5h125q5 0 5-5v-8q0-5 5-5m5 0h86m-494 0q5 0 5 5v34q0 5 5 5h5m72 0h10m42 0h10m25 0h10m113 0h10m25 0h10m132 0h5q5 0 5-5v-34q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="96" height="24" rx="7"/><text class="text" x="35" y="21">CONSTRAINT</text><a xlink:href="../grammar_diagrams#constraint-name"><rect class="rule" x="131" y="5" width="125" height="24"/><text class="text" x="141" y="21">constraint_name</text></a><rect class="literal" x="306" y="5" width="60" height="24" rx="7"/><text class="text" x="316" y="21">CHECK</text><rect class="literal" x="376" y="5" width="25" height="24" rx="7"/><text class="text" x="386" y="21">(</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="411" y="5" width="88" height="24"/><text class="text" x="421" y="21">expression</text></a><rect class="literal" x="509" y="5" width="25" height="24" rx="7"/><text class="text" x="519" y="21">)</text><rect class="literal" x="564" y="5" width="38" height="24" rx="7"/><text class="text" x="574" y="21">NO</text><rect class="literal" x="612" y="5" width="67" height="24" rx="7"/><text class="text" x="622" y="21">INHERIT</text><rect class="literal" x="306" y="49" width="72" height="24" rx="7"/><text class="text" x="316" y="65">PRIMARY</text><rect class="literal" x="388" y="49" width="42" height="24" rx="7"/><text class="text" x="398" y="65">KEY</text><rect class="literal" x="440" y="49" width="25" height="24" rx="7"/><text class="text" x="450" y="65">(</text><a xlink:href="../grammar_diagrams#column-names"><rect class="rule" x="475" y="49" width="113" height="24"/><text class="text" x="485" y="65">column_names</text></a><rect class="literal" x="598" y="49" width="25" height="24" rx="7"/><text class="text" x="608" y="65">)</text><a xlink:href="../grammar_diagrams#index-parameters"><rect class="rule" x="633" y="49" width="132" height="24"/><text class="text" x="643" y="65">index_parameters</text></a></svg>

#### column_names
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="156" height="63" viewbox="0 0 156 63"><path class="connector" d="M0 50h25m-5 0q-5 0-5-5v-19q0-5 5-5h46m24 0h46q5 0 5 5v19q0 5-5 5m-5 0h25"/><rect class="literal" x="66" y="5" width="24" height="24" rx="7"/><text class="text" x="76" y="21">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="34" width="106" height="24"/><text class="text" x="35" y="50">column_name</text></a></svg>

### Grammar
```
alter_table ::= ALTER TABLE [ IF EXISTS ] [ ONLY ] name [ * ] alter_table_action [, ... ]

alter_table_action ::=
  { ADD [ COLUMN ] [ IF NOT EXISTS ] column_name data_type |
    DROP [ COLUMN ] [ IF EXISTS ] column_name [ RESTRICT | CASCADE ] |
    ADD table_constraint |
    DROP CONSTRAINT [ IF EXISTS ]  constraint_name [ RESTRICT | CASCADE ] }

table_constraint ::=
  CONSTRAINT constraint_name { CHECK ( expression ) [ NO INHERIT ] |
                               PRIMARY KEY ( column_name [, ... ] ) index_parameters }
```

## Semantics

- An error is raised if specified table does not exist unless `IF EXIST` is used.
- `ADD COLUMN` adds new column.
- `DROP COLUMN` drops existing column.
- `ADD table_constraint` adds new table_constraint.
- `DROP table_constraint` drops existing table_constraint.
- Other `ALTER TABLE` options are not yet supported.

## See Also
[`CREATE TABLE`](../ddl_create_table)
[`DROP TABLE`](../ddl_drop_table)
[Other PostgreSQL Statements](..)
