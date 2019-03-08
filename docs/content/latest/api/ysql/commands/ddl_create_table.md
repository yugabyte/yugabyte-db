---
title: CREATE TABLE
linkTitle: CREATE TABLE
summary: Create a new table in a database
description: CREATE TABLE
menu:
  latest:
    identifier: api-ysql-commands-create-table
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/ddl_create_table
isTocNested: true
showAsideToc: true
---

## Synopsis
The `CREATE TABLE` statement creates a new table in a database. It defines the table name, column names and types, primary key, and table properties.

## Syntax

### Diagram 

#### create_table
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="701" height="78" viewbox="0 0 701 78"><path class="connector" d="M0 50h5m67 0h10m57 0h30m30 0h10m45 0h10m61 0h20m-191 0q5 0 5 5v8q0 5 5 5h166q5 0 5-5v-8q0-5 5-5m5 0h10m93 0h10m25 0h50m-5 0q-5 0-5-5v-19q0-5 5-5h37m24 0h37q5 0 5 5v19q0 5-5 5m-5 0h40m-163 0q5 0 5 5v8q0 5 5 5h138q5 0 5-5v-8q0-5 5-5m5 0h10m25 0h5"/><rect class="literal" x="5" y="34" width="67" height="24" rx="7"/><text class="text" x="15" y="50">CREATE</text><rect class="literal" x="82" y="34" width="57" height="24" rx="7"/><text class="text" x="92" y="50">TABLE</text><rect class="literal" x="169" y="34" width="30" height="24" rx="7"/><text class="text" x="179" y="50">IF</text><rect class="literal" x="209" y="34" width="45" height="24" rx="7"/><text class="text" x="219" y="50">NOT</text><rect class="literal" x="264" y="34" width="61" height="24" rx="7"/><text class="text" x="274" y="50">EXISTS</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="355" y="34" width="93" height="24"/><text class="text" x="365" y="50">table_name</text></a><rect class="literal" x="458" y="34" width="25" height="24" rx="7"/><text class="text" x="468" y="50">(</text><rect class="literal" x="565" y="5" width="24" height="24" rx="7"/><text class="text" x="575" y="21">,</text><a xlink:href="../grammar_diagrams#table-elem"><rect class="rule" x="533" y="34" width="88" height="24"/><text class="text" x="543" y="50">table_elem</text></a><rect class="literal" x="671" y="34" width="25" height="24" rx="7"/><text class="text" x="681" y="50">)</text></svg>

#### table_elem
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="473" height="93" viewbox="0 0 473 93"><path class="connector" d="M0 36h25m106 0h10m82 0h50m-5 0q-5 0-5-5v-16q0-5 5-5h145q5 0 5 5v16q0 5-5 5m-5 0h40m-210 0q5 0 5 5v8q0 5 5 5h185q5 0 5-5v-8q0-5 5-5m5 0h20m-458 0q5 0 5 5v34q0 5 5 5h5m122 0h306q5 0 5-5v-34q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="20" width="106" height="24"/><text class="text" x="35" y="36">column_name</text></a><a xlink:href="../grammar_diagrams#data-type"><rect class="rule" x="141" y="20" width="82" height="24"/><text class="text" x="151" y="36">data_type</text></a><a xlink:href="../grammar_diagrams#column-constraint"><rect class="rule" x="273" y="20" width="135" height="24"/><text class="text" x="283" y="36">column_constraint</text></a><a xlink:href="../grammar_diagrams#table-constraint"><rect class="rule" x="25" y="64" width="122" height="24"/><text class="text" x="35" y="80">table_constraint</text></a></svg>

#### table_constraint
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="790" height="78" viewbox="0 0 790 78"><path class="connector" d="M0 21h25m96 0h10m125 0h20m-266 0q5 0 5 5v8q0 5 5 5h241q5 0 5-5v-8q0-5 5-5m5 0h30m60 0h10m25 0h10m88 0h10m25 0h30m38 0h10m67 0h20m-150 0q5 0 5 5v8q0 5 5 5h125q5 0 5-5v-8q0-5 5-5m5 0h86m-494 0q5 0 5 5v34q0 5 5 5h5m72 0h10m42 0h10m25 0h10m113 0h10m25 0h10m132 0h5q5 0 5-5v-34q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="96" height="24" rx="7"/><text class="text" x="35" y="21">CONSTRAINT</text><a xlink:href="../grammar_diagrams#constraint-name"><rect class="rule" x="131" y="5" width="125" height="24"/><text class="text" x="141" y="21">constraint_name</text></a><rect class="literal" x="306" y="5" width="60" height="24" rx="7"/><text class="text" x="316" y="21">CHECK</text><rect class="literal" x="376" y="5" width="25" height="24" rx="7"/><text class="text" x="386" y="21">(</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="411" y="5" width="88" height="24"/><text class="text" x="421" y="21">expression</text></a><rect class="literal" x="509" y="5" width="25" height="24" rx="7"/><text class="text" x="519" y="21">)</text><rect class="literal" x="564" y="5" width="38" height="24" rx="7"/><text class="text" x="574" y="21">NO</text><rect class="literal" x="612" y="5" width="67" height="24" rx="7"/><text class="text" x="622" y="21">INHERIT</text><rect class="literal" x="306" y="49" width="72" height="24" rx="7"/><text class="text" x="316" y="65">PRIMARY</text><rect class="literal" x="388" y="49" width="42" height="24" rx="7"/><text class="text" x="398" y="65">KEY</text><rect class="literal" x="440" y="49" width="25" height="24" rx="7"/><text class="text" x="450" y="65">(</text><a xlink:href="../grammar_diagrams#column-names"><rect class="rule" x="475" y="49" width="113" height="24"/><text class="text" x="485" y="65">column_names</text></a><rect class="literal" x="598" y="49" width="25" height="24" rx="7"/><text class="text" x="608" y="65">)</text><a xlink:href="../grammar_diagrams#index-parameters"><rect class="rule" x="633" y="49" width="132" height="24"/><text class="text" x="643" y="65">index_parameters</text></a></svg>

#### column_constraint
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="724" height="165" viewbox="0 0 724 165"><path class="connector" d="M0 21h25m96 0h10m125 0h20m-266 0q5 0 5 5v8q0 5 5 5h241q5 0 5-5v-8q0-5 5-5m5 0h30m45 0h10m50 0h308m-423 24q0 5 5 5h5m50 0h348q5 0 5-5m-413 29q0 5 5 5h5m60 0h10m25 0h10m88 0h10m25 0h30m38 0h10m67 0h20m-150 0q5 0 5 5v8q0 5 5 5h125q5 0 5-5v-8q0-5 5-5m5 0h5q5 0 5-5m-413 44q0 5 5 5h5m74 0h10m97 0h217q5 0 5-5m-418-97q5 0 5 5v121q0 5 5 5h5m72 0h10m42 0h10m132 0h132q5 0 5-5v-121q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="96" height="24" rx="7"/><text class="text" x="35" y="21">CONSTRAINT</text><a xlink:href="../grammar_diagrams#constraint-name"><rect class="rule" x="131" y="5" width="125" height="24"/><text class="text" x="141" y="21">constraint_name</text></a><rect class="literal" x="306" y="5" width="45" height="24" rx="7"/><text class="text" x="316" y="21">NOT</text><rect class="literal" x="361" y="5" width="50" height="24" rx="7"/><text class="text" x="371" y="21">NULL</text><rect class="literal" x="306" y="34" width="50" height="24" rx="7"/><text class="text" x="316" y="50">NULL</text><rect class="literal" x="306" y="63" width="60" height="24" rx="7"/><text class="text" x="316" y="79">CHECK</text><rect class="literal" x="376" y="63" width="25" height="24" rx="7"/><text class="text" x="386" y="79">(</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="411" y="63" width="88" height="24"/><text class="text" x="421" y="79">expression</text></a><rect class="literal" x="509" y="63" width="25" height="24" rx="7"/><text class="text" x="519" y="79">)</text><rect class="literal" x="564" y="63" width="38" height="24" rx="7"/><text class="text" x="574" y="79">NO</text><rect class="literal" x="612" y="63" width="67" height="24" rx="7"/><text class="text" x="622" y="79">INHERIT</text><rect class="literal" x="306" y="107" width="74" height="24" rx="7"/><text class="text" x="316" y="123">DEFAULT</text><a xlink:href="../grammar_diagrams#default-expr"><rect class="rule" x="390" y="107" width="97" height="24"/><text class="text" x="400" y="123">default_expr</text></a><rect class="literal" x="306" y="136" width="72" height="24" rx="7"/><text class="text" x="316" y="152">PRIMARY</text><rect class="literal" x="388" y="136" width="42" height="24" rx="7"/><text class="text" x="398" y="152">KEY</text><a xlink:href="../grammar_diagrams#index-parameters"><rect class="rule" x="440" y="136" width="132" height="24"/><text class="text" x="450" y="152">index_parameters</text></a></svg>

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
Example 1

```sql
postgres=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

Example 2

```sql
postgres=# CREATE TABLE student_grade (student_id int, class_id int, term_id int, grade int CHECK (grade >= 0 AND grade <= 10), PRIMARY KEY (student_id, class_id, term_id));
```

Example 3

```sql
postgres=# CREATE TABLE cars (id int PRIMARY KEY, brand text CHECK (brand in ('X', 'Y', 'Z')), model text NOT NULL, color text NOT NULL DEFAULT 'WHITE' CHECK (color in ('RED', 'WHITE', 'BLUE')));
```

## See Also
[`DROP TABLE`](../ddl_drop_table)
[`ALTER TABLE`](../ddl_alter_table)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[Other PostgreSQL Statements](..)
