---
title: CREATE TABLE
summary: Create a new table in a keyspace
description: CREATE TABLE
menu:
  v1.0:
    identifier: api-postgresql-create-table
    parent: api-postgresql-ddl
---

## Synopsis
The `CREATE TABLE` statement creates a new table in a database. It defines the table name, column names and types, primary key, and table properties.

## Syntax

### Diagram 

#### create_table

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="683" height="80" viewbox="0 0 683 80"><path class="connector" d="M0 52h5m67 0h10m58 0h30m32 0h10m45 0h10m64 0h20m-196 0q5 0 5 5v8q0 5 5 5h171q5 0 5-5v-8q0-5 5-5m5 0h10m91 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h5"/><rect class="literal" x="5" y="35" width="67" height="25" rx="7"/><text class="text" x="15" y="52">CREATE</text><rect class="literal" x="82" y="35" width="58" height="25" rx="7"/><text class="text" x="92" y="52">TABLE</text><rect class="literal" x="170" y="35" width="32" height="25" rx="7"/><text class="text" x="180" y="52">IF</text><rect class="literal" x="212" y="35" width="45" height="25" rx="7"/><text class="text" x="222" y="52">NOT</text><rect class="literal" x="267" y="35" width="64" height="25" rx="7"/><text class="text" x="277" y="52">EXISTS</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="361" y="35" width="91" height="25"/><text class="text" x="371" y="52">table_name</text></a><rect class="literal" x="462" y="35" width="25" height="25" rx="7"/><text class="text" x="472" y="52">(</text><rect class="literal" x="558" y="5" width="24" height="25" rx="7"/><text class="text" x="568" y="22">,</text><a xlink:href="../grammar_diagrams#table-element"><rect class="rule" x="517" y="35" width="106" height="25"/><text class="text" x="527" y="52">table_element</text></a><rect class="literal" x="653" y="35" width="25" height="25" rx="7"/><text class="text" x="663" y="52">)</text></svg>

#### table_element

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="173" height="65" viewbox="0 0 173 65"><path class="connector" d="M0 22h25m101 0h42m-158 0q5 0 5 5v20q0 5 5 5h5m123 0h5q5 0 5-5v-20q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#table-column"><rect class="rule" x="25" y="5" width="101" height="25"/><text class="text" x="35" y="22">table_column</text></a><a xlink:href="../grammar_diagrams#table-constraints"><rect class="rule" x="25" y="35" width="123" height="25"/><text class="text" x="35" y="52">table_constraints</text></a></svg>

#### table_column

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="446" height="65" viewbox="0 0 446 65"><path class="connector" d="M0 37h5m106 0h10m98 0h50m-5 0q-5 0-5-5v-17q0-5 5-5h142q5 0 5 5v17q0 5-5 5m-5 0h40m-207 0q5 0 5 5v8q0 5 5 5h182q5 0 5-5v-8q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="5" y="20" width="106" height="25"/><text class="text" x="15" y="37">column_name</text></a><a xlink:href="../grammar_diagrams#column-type"><rect class="rule" x="121" y="20" width="98" height="25"/><text class="text" x="131" y="37">column_type</text></a><a xlink:href="../grammar_diagrams#column-constraint"><rect class="rule" x="269" y="20" width="132" height="25"/><text class="text" x="279" y="37">column_constraint</text></a></svg>

#### column_constraint

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="136" height="35" viewbox="0 0 136 35"><path class="connector" d="M0 22h5m73 0h10m43 0h5"/><rect class="literal" x="5" y="5" width="73" height="25" rx="7"/><text class="text" x="15" y="22">PRIMARY</text><rect class="literal" x="88" y="5" width="43" height="25" rx="7"/><text class="text" x="98" y="22">KEY</text></svg>

#### table_constraints

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="305" height="35" viewbox="0 0 305 35"><path class="connector" d="M0 22h5m73 0h10m43 0h10m25 0h10m89 0h10m25 0h5"/><rect class="literal" x="5" y="5" width="73" height="25" rx="7"/><text class="text" x="15" y="22">PRIMARY</text><rect class="literal" x="88" y="5" width="43" height="25" rx="7"/><text class="text" x="98" y="22">KEY</text><rect class="literal" x="141" y="5" width="25" height="25" rx="7"/><text class="text" x="151" y="22">(</text><a xlink:href="../grammar_diagrams#column-list"><rect class="rule" x="176" y="5" width="89" height="25"/><text class="text" x="186" y="22">column_list</text></a><rect class="literal" x="275" y="5" width="25" height="25" rx="7"/><text class="text" x="285" y="22">)</text></svg>

#### column_list

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="156" height="65" viewbox="0 0 156 65"><path class="connector" d="M0 52h25m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h25"/><rect class="literal" x="66" y="5" width="24" height="25" rx="7"/><text class="text" x="76" y="22">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="35" width="106" height="25"/><text class="text" x="35" y="52">column_name</text></a></svg>

### Grammar
```
create_table ::= CREATE TABLE [ IF NOT EXISTS ] table_name
                     '(' table_element [ ',' table_element ...] ')';

table_element ::= table_column | table_constraints

table_column ::= column_name column_type [ column_constraint ...]

column_constraint ::= PRIMARY KEY

table_constraints ::= PRIMARY KEY '(' column_list ')'

column_list ::= column_name [ ',' column_name ...]

```

Where

- `table_name` and `column_name` are identifiers (`table_name` can be a qualified name).

## Semantics
- An error is raised if `table_name` already exists in the specified database unless the `IF NOT EXISTS` option is used.

### PRIMARY KEY
- Primary key can be defined in either `column_constraint` or `table_constraint` but not in both of them.
- Each row in a table is uniquely identified by its primary key. 

## See Also
[`DELETE`](../dml_delete)
[`DROP TABLE`](../ddl_drop_table)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[Other PostgreSQL Statements](..)
