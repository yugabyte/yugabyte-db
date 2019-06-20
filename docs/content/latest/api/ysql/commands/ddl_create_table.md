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
  - /latest/api/ysql/ddl_create_table/
isTocNested: true
showAsideToc: true
---

## Synopsis
The `CREATE TABLE` command creates a new table in a database. It defines the table name, column names and types, primary key, and table properties.

## Syntax

### Diagrams

#### create_table
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="701" height="78" viewbox="0 0 701 78"><path class="connector" d="M0 50h5m67 0h10m57 0h30m30 0h10m45 0h10m61 0h20m-191 0q5 0 5 5v8q0 5 5 5h166q5 0 5-5v-8q0-5 5-5m5 0h10m93 0h10m25 0h50m-5 0q-5 0-5-5v-19q0-5 5-5h37m24 0h37q5 0 5 5v19q0 5-5 5m-5 0h40m-163 0q5 0 5 5v8q0 5 5 5h138q5 0 5-5v-8q0-5 5-5m5 0h10m25 0h5"/><rect class="literal" x="5" y="34" width="67" height="24" rx="7"/><text class="text" x="15" y="50">CREATE</text><rect class="literal" x="82" y="34" width="57" height="24" rx="7"/><text class="text" x="92" y="50">TABLE</text><rect class="literal" x="169" y="34" width="30" height="24" rx="7"/><text class="text" x="179" y="50">IF</text><rect class="literal" x="209" y="34" width="45" height="24" rx="7"/><text class="text" x="219" y="50">NOT</text><rect class="literal" x="264" y="34" width="61" height="24" rx="7"/><text class="text" x="274" y="50">EXISTS</text><a xlink:href="../../grammar_diagrams#table-name"><rect class="rule" x="355" y="34" width="93" height="24"/><text class="text" x="365" y="50">table_name</text></a><rect class="literal" x="458" y="34" width="25" height="24" rx="7"/><text class="text" x="468" y="50">(</text><rect class="literal" x="565" y="5" width="24" height="24" rx="7"/><text class="text" x="575" y="21">,</text><a xlink:href="../../grammar_diagrams#table-elem"><rect class="rule" x="533" y="34" width="88" height="24"/><text class="text" x="543" y="50">table_elem</text></a><rect class="literal" x="671" y="34" width="25" height="24" rx="7"/><text class="text" x="681" y="50">)</text></svg>

#### table_elem
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="473" height="93" viewbox="0 0 473 93"><path class="connector" d="M0 36h25m106 0h10m82 0h50m-5 0q-5 0-5-5v-16q0-5 5-5h145q5 0 5 5v16q0 5-5 5m-5 0h40m-210 0q5 0 5 5v8q0 5 5 5h185q5 0 5-5v-8q0-5 5-5m5 0h20m-458 0q5 0 5 5v34q0 5 5 5h5m122 0h306q5 0 5-5v-34q0-5 5-5m5 0h5"/><a xlink:href="../../grammar_diagrams#column-name"><rect class="rule" x="25" y="20" width="106" height="24"/><text class="text" x="35" y="36">column_name</text></a><a xlink:href="../../grammar_diagrams#data-type"><rect class="rule" x="141" y="20" width="82" height="24"/><text class="text" x="151" y="36">data_type</text></a><a xlink:href="../../grammar_diagrams#column-constraint"><rect class="rule" x="273" y="20" width="135" height="24"/><text class="text" x="283" y="36">column_constraint</text></a><a xlink:href="../../grammar_diagrams#table-constraint"><rect class="rule" x="25" y="64" width="122" height="24"/><text class="text" x="35" y="80">table_constraint</text></a></svg>

#### column_constraint
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="554" height="190" viewbox="0 0 554 190"><path class="connector" d="M0 22h25m98 0h10m122 0h20m-265 0q5 0 5 5v8q0 5 5 5h240q5 0 5-5v-8q0-5 5-5m5 0h30m45 0h10m52 0h137m-254 25q0 5 5 5h5m52 0h177q5 0 5-5m-244 30q0 5 5 5h5m61 0h10m25 0h10m83 0h10m25 0h5q5 0 5-5m-244 30q0 5 5 5h5m75 0h10m83 0h61q5 0 5-5m-244 30q0 5 5 5h5m73 0h10m43 0h103q5 0 5-5m-244 13q0 5 5 5h234q5 0 5-5m-249-128q5 0 5 5v145q0 5 5 5h5m127 0h102q5 0 5-5v-145q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="98" height="25" rx="7"/><text class="text" x="35" y="22">CONSTRAINT</text><a xlink:href="../../grammar_diagrams#constraint-name"><rect class="rule" x="133" y="5" width="122" height="25"/><text class="text" x="143" y="22">constraint_name</text></a><rect class="literal" x="305" y="5" width="45" height="25" rx="7"/><text class="text" x="315" y="22">NOT</text><rect class="literal" x="360" y="5" width="52" height="25" rx="7"/><text class="text" x="370" y="22">NULL</text><rect class="literal" x="305" y="35" width="52" height="25" rx="7"/><text class="text" x="315" y="52">NULL</text><rect class="literal" x="305" y="65" width="61" height="25" rx="7"/><text class="text" x="315" y="82">CHECK</text><rect class="literal" x="376" y="65" width="25" height="25" rx="7"/><text class="text" x="386" y="82">(</text><a xlink:href="../../grammar_diagrams#expression"><rect class="rule" x="411" y="65" width="83" height="25"/><text class="text" x="421" y="82">expression</text></a><rect class="literal" x="504" y="65" width="25" height="25" rx="7"/><text class="text" x="514" y="82">)</text><rect class="literal" x="305" y="95" width="75" height="25" rx="7"/><text class="text" x="315" y="112">DEFAULT</text><a xlink:href="../../grammar_diagrams#expression"><rect class="rule" x="390" y="95" width="83" height="25"/><text class="text" x="400" y="112">expression</text></a><rect class="literal" x="305" y="125" width="73" height="25" rx="7"/><text class="text" x="315" y="142">PRIMARY</text><rect class="literal" x="388" y="125" width="43" height="25" rx="7"/><text class="text" x="398" y="142">KEY</text><a xlink:href="../../grammar_diagrams#references-clause"><rect class="rule" x="305" y="160" width="127" height="25"/><text class="text" x="315" y="177">references_clause</text></a></svg>

#### table_constraint
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="787" height="95" viewbox="0 0 787 95"><path class="connector" d="M0 22h25m98 0h10m122 0h20m-265 0q5 0 5 5v8q0 5 5 5h240q5 0 5-5v-8q0-5 5-5m5 0h30m61 0h10m25 0h10m83 0h10m25 0h253m-487 25q0 5 5 5h5m73 0h10m43 0h10m25 0h10m112 0h10m25 0h144q5 0 5-5m-482-25q5 0 5 5v50q0 5 5 5h5m75 0h10m43 0h10m25 0h10m112 0h10m25 0h10m127 0h5q5 0 5-5v-50q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="98" height="25" rx="7"/><text class="text" x="35" y="22">CONSTRAINT</text><a xlink:href="../../grammar_diagrams#constraint-name"><rect class="rule" x="133" y="5" width="122" height="25"/><text class="text" x="143" y="22">constraint_name</text></a><rect class="literal" x="305" y="5" width="61" height="25" rx="7"/><text class="text" x="315" y="22">CHECK</text><rect class="literal" x="376" y="5" width="25" height="25" rx="7"/><text class="text" x="386" y="22">(</text><a xlink:href="../../grammar_diagrams#expression"><rect class="rule" x="411" y="5" width="83" height="25"/><text class="text" x="421" y="22">expression</text></a><rect class="literal" x="504" y="5" width="25" height="25" rx="7"/><text class="text" x="514" y="22">)</text><rect class="literal" x="305" y="35" width="73" height="25" rx="7"/><text class="text" x="315" y="52">PRIMARY</text><rect class="literal" x="388" y="35" width="43" height="25" rx="7"/><text class="text" x="398" y="52">KEY</text><rect class="literal" x="441" y="35" width="25" height="25" rx="7"/><text class="text" x="451" y="52">(</text><a xlink:href="../../grammar_diagrams#column-names"><rect class="rule" x="476" y="35" width="112" height="25"/><text class="text" x="486" y="52">column_names</text></a><rect class="literal" x="598" y="35" width="25" height="25" rx="7"/><text class="text" x="608" y="52">)</text><rect class="literal" x="305" y="65" width="75" height="25" rx="7"/><text class="text" x="315" y="82">FOREIGN</text><rect class="literal" x="390" y="65" width="43" height="25" rx="7"/><text class="text" x="400" y="82">KEY</text><rect class="literal" x="443" y="65" width="25" height="25" rx="7"/><text class="text" x="453" y="82">(</text><a xlink:href="../../grammar_diagrams#column-names"><rect class="rule" x="478" y="65" width="112" height="25"/><text class="text" x="488" y="82">column_names</text></a><rect class="literal" x="600" y="65" width="25" height="25" rx="7"/><text class="text" x="610" y="82">)</text><a xlink:href="../../grammar_diagrams#references-clause"><rect class="rule" x="635" y="65" width="127" height="25"/><text class="text" x="645" y="82">references_clause</text></a></svg>

### Grammar
```
create_table ::= CREATE TABLE [ IF NOT EXISTS ] table_name ( table_elem [, ...] )

table_elem ::= column_name data_type [ column_constraint ...] | table_constraint

column_constraint ::= [ CONSTRAINT constraint_name ]
                      { NOT NULL |
                        NULL |
                        CHECK ( expression ) |
                        DEFAULT expression |
                        PRIMARY KEY | 
                        references_clause } ;

table_constraint ::= [ CONSTRAINT constraint_name ] 
                     { CHECK ( expression ) |
                       PRIMARY KEY ( column_names ) |
                       FOREIGN KEY ( column_names ) references_clause }
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

### FOREIGN KEY

Foreign keys are supported starting v1.2.10.

## Examples

Table with primary key

```sql
postgres=# CREATE TABLE sample(k1 int, 
                               k2 int, 
                               v1 int, 
                               v2 text, 
                               PRIMARY KEY (k1, k2));
```

Table with check constraint

```sql
postgres=# CREATE TABLE student_grade (student_id int, 
                                       class_id int, 
                                       term_id int, 
                                       grade int CHECK (grade >= 0 AND grade <= 10), 
                                       PRIMARY KEY (student_id, class_id, term_id));
```

Table with default value

```sql
postgres=# CREATE TABLE cars (id int PRIMARY KEY, 
                              brand text CHECK (brand in ('X', 'Y', 'Z')), 
                              model text NOT NULL, 
                              color text NOT NULL DEFAULT 'WHITE' CHECK (color in ('RED', 'WHITE', 'BLUE')));
```

Table with foreign key constraint

```sql
postgres=# create table products(id int primary key, 
                                 descr text);
postgres=# create table orders(id int primary key, 
                               pid int references products(id) ON DELETE CASCADE, 
                               amount int);
```

## See Also
[`DROP TABLE`](../ddl_drop_table)
[`ALTER TABLE`](../ddl_alter_table)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[Other YSQL Statements](..)
