---
title: CREATE TABLE
summary: Create a new table in a database
description: CREATE TABLE
menu:
  latest:
    identifier: api-postgresql-create-table
    parent: api-postgresql-ddl
aliases:
  - /latest/api/postgresql/ddl_create_table
  - /latest/api/ysql/ddl_create_table
isTocNested: true
showAsideToc: true
---

## Synopsis
The `CREATE TABLE` statement creates a new table in a database. It defines the table name, column names and types, primary key, and table properties.

## Syntax

### Diagram 

#### create_table

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="492" height="65" viewbox="0 0 492 65"><path class="connector" d="M0 52h5m67 0h10m58 0h10m111 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h5"/><rect class="literal" x="5" y="35" width="67" height="25" rx="7"/><text class="text" x="15" y="52">CREATE</text><rect class="literal" x="82" y="35" width="58" height="25" rx="7"/><text class="text" x="92" y="52">TABLE</text><a xlink:href="../grammar_diagrams#qualified-name"><rect class="rule" x="150" y="35" width="111" height="25"/><text class="text" x="160" y="52">qualified_name</text></a><rect class="literal" x="271" y="35" width="25" height="25" rx="7"/><text class="text" x="281" y="52">(</text><rect class="literal" x="367" y="5" width="24" height="25" rx="7"/><text class="text" x="377" y="22">,</text><a xlink:href="../grammar_diagrams#table-element"><rect class="rule" x="326" y="35" width="106" height="25"/><text class="text" x="336" y="52">table_element</text></a><rect class="literal" x="462" y="35" width="25" height="25" rx="7"/><text class="text" x="472" y="52">)</text></svg>

#### table_element

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="173" height="65" viewbox="0 0 173 65"><path class="connector" d="M0 22h25m101 0h42m-158 0q5 0 5 5v20q0 5 5 5h5m123 0h5q5 0 5-5v-20q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#table-column"><rect class="rule" x="25" y="5" width="101" height="25"/><text class="text" x="35" y="22">table_column</text></a><a xlink:href="../grammar_diagrams#table-constraints"><rect class="rule" x="25" y="35" width="123" height="25"/><text class="text" x="35" y="52">table_constraints</text></a></svg>

#### table_column

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="507" height="95" viewbox="0 0 507 95"><path class="connector" d="M0 37h5m106 0h10m98 0h50m-5 0q-5 0-5-5v-17q0-5 5-5h203q5 0 5 5v17q0 5-5 5m-198 0h20m132 0h41m-188 0q5 0 5 5v20q0 5 5 5h5m153 0h5q5 0 5-5v-20q0-5 5-5m5 0h40m-268 0q5 0 5 5v38q0 5 5 5h243q5 0 5-5v-38q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="5" y="20" width="106" height="25"/><text class="text" x="15" y="37">column_name</text></a><a xlink:href="../grammar_diagrams#column-type"><rect class="rule" x="121" y="20" width="98" height="25"/><text class="text" x="131" y="37">column_type</text></a><a xlink:href="../grammar_diagrams#column-constraint"><rect class="rule" x="289" y="20" width="132" height="25"/><text class="text" x="299" y="37">column_constraint</text></a><a xlink:href="../grammar_diagrams#column-default-value"><rect class="rule" x="289" y="50" width="153" height="25"/><text class="text" x="299" y="67">column_default_value</text></a></svg>

#### column_constraint

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="274" height="95" viewbox="0 0 274 95"><path class="connector" d="M0 22h25m73 0h10m43 0h118m-254 25q0 5 5 5h5m45 0h10m52 0h122q5 0 5-5m-249-25q5 0 5 5v50q0 5 5 5h5m61 0h10m25 0h10m83 0h10m25 0h5q5 0 5-5v-50q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="73" height="25" rx="7"/><text class="text" x="35" y="22">PRIMARY</text><rect class="literal" x="108" y="5" width="43" height="25" rx="7"/><text class="text" x="118" y="22">KEY</text><rect class="literal" x="25" y="35" width="45" height="25" rx="7"/><text class="text" x="35" y="52">NOT</text><rect class="literal" x="80" y="35" width="52" height="25" rx="7"/><text class="text" x="90" y="52">NULL</text><rect class="literal" x="25" y="65" width="61" height="25" rx="7"/><text class="text" x="35" y="82">CHECK</text><rect class="literal" x="96" y="65" width="25" height="25" rx="7"/><text class="text" x="106" y="82">(</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="131" y="65" width="83" height="25"/><text class="text" x="141" y="82">expression</text></a><rect class="literal" x="224" y="65" width="25" height="25" rx="7"/><text class="text" x="234" y="82">)</text></svg>

### column_default_value

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="178" height="35" viewbox="0 0 178 35"><path class="connector" d="M0 22h5m75 0h10m83 0h5"/><rect class="literal" x="5" y="5" width="75" height="25" rx="7"/><text class="text" x="15" y="22">DEFAULT</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="90" y="5" width="83" height="25"/><text class="text" x="100" y="22">expression</text></a></svg>

#### table_constraints

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="305" height="35" viewbox="0 0 305 35"><path class="connector" d="M0 22h5m73 0h10m43 0h10m25 0h10m89 0h10m25 0h5"/><rect class="literal" x="5" y="5" width="73" height="25" rx="7"/><text class="text" x="15" y="22">PRIMARY</text><rect class="literal" x="88" y="5" width="43" height="25" rx="7"/><text class="text" x="98" y="22">KEY</text><rect class="literal" x="141" y="5" width="25" height="25" rx="7"/><text class="text" x="151" y="22">(</text><a xlink:href="../grammar_diagrams#column-list"><rect class="rule" x="176" y="5" width="89" height="25"/><text class="text" x="186" y="22">column_list</text></a><rect class="literal" x="275" y="5" width="25" height="25" rx="7"/><text class="text" x="285" y="22">)</text></svg>

#### column_list

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="104" height="65" viewbox="0 0 104 65"><path class="connector" d="M0 52h25m-5 0q-5 0-5-5v-20q0-5 5-5h20m24 0h20q5 0 5 5v20q0 5-5 5m-5 0h25"/><rect class="literal" x="40" y="5" width="24" height="25" rx="7"/><text class="text" x="50" y="22">,</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="25" y="35" width="54" height="25"/><text class="text" x="35" y="52">name</text></a></svg>

### Grammar
```
create_table ::= CREATE TABLE qualified_name '(' table_element [ ',' table_element ...] ')';

table_element ::= table_column | table_constraints

table_column ::= name column_type [ column_constraint ... | default_value ]

column_constraint ::= PRIMARY KEY | NOT NULL | CHECK '(' expression ')'

default_value ::= DEFAULT expression

table_constraints ::= PRIMARY KEY '(' column_list ')'

column_list ::= name [ ',' name ...]
```

Where

- `qualified_name` and `name` are identifiers (`qualified_name` can be a qualified name).
- `expression` for DEFAULT keyword must be of the same type as the column it modifies. It must be of type boolean for CHECK constraints.

## Semantics
- An error is raised if `qualified_name` already exists in the specified database.

### PRIMARY KEY
- Currently defining a primary key is required.
- Primary key can be defined in either `column_constraint` or `table_constraint` but not in both of them.
- Each row in a table is uniquely identified by its primary key. 

## Examples

```{.sql .copy .separator-hash}
postgres=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```
```{.sql .copy .separator-hash}
postgres=# CREATE TABLE student_grade (student_id int, class_id int, term_id int, grade int CHECK (grade >= 0 AND grade <= 10), PRIMARY KEY (student_id, class_id, term_id));
```

```{.sql .copy .separator-hash}
postgres=# CREATE TABLE cars (id int PRIMARY KEY, brand text CHECK (brand in ('X', 'Y', 'Z')), model text NOT NULL, color text NOT NULL DEFAULT 'WHITE' CHECK (color in ('RED', 'WHITE', 'BLUE')));
```

## See Also
[`DROP TABLE`](../ddl_drop_table)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[Other PostgreSQL Statements](..)
