---
title: CREATE TABLE
linkTitle: CREATE TABLE
summary: Create a new table in a database
description: CREATE TABLE
block_indexing: true
menu:
  v1.2:
    identifier: api-ysql-commands-create-table
    parent: api-ysql-commands
isTocNested: true
showAsideToc: true
---

## Synopsis
The `CREATE TABLE` command creates a new table in a database. It defines the table name, column names and types, primary key, and table properties.

## Syntax

### Diagrams

#### create_table
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="1080" height="100" viewbox="0 0 1080 100"><path class="connector" d="M0 52h15m67 0h10m58 0h30m32 0h10m45 0h10m64 0h20m-196 0q5 0 5 5v8q0 5 5 5h171q5 0 5-5v-8q0-5 5-5m5 0h10m91 0h10m25 0h50m-5 0q-5 0-5-5v-20q0-5 5-5h36m24 0h36q5 0 5 5v20q0 5-5 5m-5 0h40m-161 0q5 0 5 5v8q0 5 5 5h136q5 0 5-5v-8q0-5 5-5m5 0h10m25 0h30m53 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h60m24 0h60q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h20m-337 25q0 5 5 5h5m78 0h10m51 0h173q5 0 5-5m-332-25q5 0 5 5v33q0 5 5 5h317q5 0 5-5v-33q0-5 5-5m5 0h15"/><polygon points="0,59 5,52 0,45" style="fill:black;stroke-width:0"/><rect class="literal" x="15" y="35" width="67" height="25" rx="7"/><text class="text" x="25" y="52">CREATE</text><rect class="literal" x="92" y="35" width="58" height="25" rx="7"/><text class="text" x="102" y="52">TABLE</text><rect class="literal" x="180" y="35" width="32" height="25" rx="7"/><text class="text" x="190" y="52">IF</text><rect class="literal" x="222" y="35" width="45" height="25" rx="7"/><text class="text" x="232" y="52">NOT</text><rect class="literal" x="277" y="35" width="64" height="25" rx="7"/><text class="text" x="287" y="52">EXISTS</text><a xlink:href="../../grammar_diagrams#table-name"><rect class="rule" x="371" y="35" width="91" height="25"/><text class="text" x="381" y="52">table_name</text></a><rect class="literal" x="472" y="35" width="25" height="25" rx="7"/><text class="text" x="482" y="52">(</text><rect class="literal" x="578" y="5" width="24" height="25" rx="7"/><text class="text" x="588" y="22">,</text><a xlink:href="../../grammar_diagrams#table-elem"><rect class="rule" x="547" y="35" width="86" height="25"/><text class="text" x="557" y="52">table_elem</text></a><rect class="literal" x="683" y="35" width="25" height="25" rx="7"/><text class="text" x="693" y="52">)</text><rect class="literal" x="738" y="35" width="53" height="25" rx="7"/><text class="text" x="748" y="52">WITH</text><rect class="literal" x="801" y="35" width="25" height="25" rx="7"/><text class="text" x="811" y="52">(</text><rect class="literal" x="911" y="5" width="24" height="25" rx="7"/><text class="text" x="921" y="22">,</text><a xlink:href="../../grammar_diagrams#storage-parameter"><rect class="rule" x="856" y="35" width="134" height="25"/><text class="text" x="866" y="52">storage_parameter</text></a><rect class="literal" x="1020" y="35" width="25" height="25" rx="7"/><text class="text" x="1030" y="52">)</text><rect class="literal" x="738" y="65" width="78" height="25" rx="7"/><text class="text" x="748" y="82">WITHOUT</text><rect class="literal" x="826" y="65" width="51" height="25" rx="7"/><text class="text" x="836" y="82">OIDS</text><polygon points="1076,59 1080,59 1080,45 1076,45" style="fill:black;stroke-width:0"/></svg>

#### table_elem
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="473" height="93" viewbox="0 0 473 93"><path class="connector" d="M0 36h25m106 0h10m82 0h50m-5 0q-5 0-5-5v-16q0-5 5-5h145q5 0 5 5v16q0 5-5 5m-5 0h40m-210 0q5 0 5 5v8q0 5 5 5h185q5 0 5-5v-8q0-5 5-5m5 0h20m-458 0q5 0 5 5v34q0 5 5 5h5m122 0h306q5 0 5-5v-34q0-5 5-5m5 0h5"/><a xlink:href="../../grammar_diagrams#column-name"><rect class="rule" x="25" y="20" width="106" height="24"/><text class="text" x="35" y="36">column_name</text></a><a xlink:href="../../grammar_diagrams#data-type"><rect class="rule" x="141" y="20" width="82" height="24"/><text class="text" x="151" y="36">data_type</text></a><a xlink:href="../../grammar_diagrams#column-constraint"><rect class="rule" x="273" y="20" width="135" height="24"/><text class="text" x="283" y="36">column_constraint</text></a><a xlink:href="../../grammar_diagrams#table-constraint"><rect class="rule" x="25" y="64" width="122" height="24"/><text class="text" x="35" y="80">table_constraint</text></a></svg>

#### column_constraint
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="554" height="190" viewbox="0 0 554 190"><path class="connector" d="M0 22h25m98 0h10m122 0h20m-265 0q5 0 5 5v8q0 5 5 5h240q5 0 5-5v-8q0-5 5-5m5 0h30m45 0h10m52 0h137m-254 25q0 5 5 5h5m52 0h177q5 0 5-5m-244 30q0 5 5 5h5m61 0h10m25 0h10m83 0h10m25 0h5q5 0 5-5m-244 30q0 5 5 5h5m75 0h10m83 0h61q5 0 5-5m-244 30q0 5 5 5h5m73 0h10m43 0h103q5 0 5-5m-244 13q0 5 5 5h234q5 0 5-5m-249-128q5 0 5 5v145q0 5 5 5h5m127 0h102q5 0 5-5v-145q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="98" height="25" rx="7"/><text class="text" x="35" y="22">CONSTRAINT</text><a xlink:href="../../grammar_diagrams#constraint-name"><rect class="rule" x="133" y="5" width="122" height="25"/><text class="text" x="143" y="22">constraint_name</text></a><rect class="literal" x="305" y="5" width="45" height="25" rx="7"/><text class="text" x="315" y="22">NOT</text><rect class="literal" x="360" y="5" width="52" height="25" rx="7"/><text class="text" x="370" y="22">NULL</text><rect class="literal" x="305" y="35" width="52" height="25" rx="7"/><text class="text" x="315" y="52">NULL</text><rect class="literal" x="305" y="65" width="61" height="25" rx="7"/><text class="text" x="315" y="82">CHECK</text><rect class="literal" x="376" y="65" width="25" height="25" rx="7"/><text class="text" x="386" y="82">(</text><a xlink:href="../../grammar_diagrams#expression"><rect class="rule" x="411" y="65" width="83" height="25"/><text class="text" x="421" y="82">expression</text></a><rect class="literal" x="504" y="65" width="25" height="25" rx="7"/><text class="text" x="514" y="82">)</text><rect class="literal" x="305" y="95" width="75" height="25" rx="7"/><text class="text" x="315" y="112">DEFAULT</text><a xlink:href="../../grammar_diagrams#expression"><rect class="rule" x="390" y="95" width="83" height="25"/><text class="text" x="400" y="112">expression</text></a><rect class="literal" x="305" y="125" width="73" height="25" rx="7"/><text class="text" x="315" y="142">PRIMARY</text><rect class="literal" x="388" y="125" width="43" height="25" rx="7"/><text class="text" x="398" y="142">KEY</text><a xlink:href="../../grammar_diagrams#references-clause"><rect class="rule" x="305" y="160" width="127" height="25"/><text class="text" x="315" y="177">references_clause</text></a></svg>

#### table_constraint
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="787" height="95" viewbox="0 0 787 95"><path class="connector" d="M0 22h25m98 0h10m122 0h20m-265 0q5 0 5 5v8q0 5 5 5h240q5 0 5-5v-8q0-5 5-5m5 0h30m61 0h10m25 0h10m83 0h10m25 0h253m-487 25q0 5 5 5h5m73 0h10m43 0h10m25 0h10m112 0h10m25 0h144q5 0 5-5m-482-25q5 0 5 5v50q0 5 5 5h5m75 0h10m43 0h10m25 0h10m112 0h10m25 0h10m127 0h5q5 0 5-5v-50q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="98" height="25" rx="7"/><text class="text" x="35" y="22">CONSTRAINT</text><a xlink:href="../../grammar_diagrams#constraint-name"><rect class="rule" x="133" y="5" width="122" height="25"/><text class="text" x="143" y="22">constraint_name</text></a><rect class="literal" x="305" y="5" width="61" height="25" rx="7"/><text class="text" x="315" y="22">CHECK</text><rect class="literal" x="376" y="5" width="25" height="25" rx="7"/><text class="text" x="386" y="22">(</text><a xlink:href="../../grammar_diagrams#expression"><rect class="rule" x="411" y="5" width="83" height="25"/><text class="text" x="421" y="22">expression</text></a><rect class="literal" x="504" y="5" width="25" height="25" rx="7"/><text class="text" x="514" y="22">)</text><rect class="literal" x="305" y="35" width="73" height="25" rx="7"/><text class="text" x="315" y="52">PRIMARY</text><rect class="literal" x="388" y="35" width="43" height="25" rx="7"/><text class="text" x="398" y="52">KEY</text><rect class="literal" x="441" y="35" width="25" height="25" rx="7"/><text class="text" x="451" y="52">(</text><a xlink:href="../../grammar_diagrams#column-names"><rect class="rule" x="476" y="35" width="112" height="25"/><text class="text" x="486" y="52">column_names</text></a><rect class="literal" x="598" y="35" width="25" height="25" rx="7"/><text class="text" x="608" y="52">)</text><rect class="literal" x="305" y="65" width="75" height="25" rx="7"/><text class="text" x="315" y="82">FOREIGN</text><rect class="literal" x="390" y="65" width="43" height="25" rx="7"/><text class="text" x="400" y="82">KEY</text><rect class="literal" x="443" y="65" width="25" height="25" rx="7"/><text class="text" x="453" y="82">(</text><a xlink:href="../../grammar_diagrams#column-names"><rect class="rule" x="478" y="65" width="112" height="25"/><text class="text" x="488" y="82">column_names</text></a><rect class="literal" x="600" y="65" width="25" height="25" rx="7"/><text class="text" x="610" y="82">)</text><a xlink:href="../../grammar_diagrams#references-clause"><rect class="rule" x="635" y="65" width="127" height="25"/><text class="text" x="645" y="82">references_clause</text></a></svg>

#### storage_parameter
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="318" height="50" viewbox="0 0 318 50"><path class="connector" d="M0 22h15m100 0h30m30 0h10m98 0h20m-173 0q5 0 5 5v8q0 5 5 5h148q5 0 5-5v-8q0-5 5-5m5 0h15"/><polygon points="0,29 5,22 0,15" style="fill:black;stroke-width:0"/><a xlink:href="../../grammar_diagrams#param-name"><rect class="rule" x="15" y="5" width="100" height="25"/><text class="text" x="25" y="22">param_name</text></a><rect class="literal" x="145" y="5" width="30" height="25" rx="7"/><text class="text" x="155" y="22">=</text><a xlink:href="../../grammar_diagrams#param-value"><rect class="rule" x="185" y="5" width="98" height="25"/><text class="text" x="195" y="22">param_value</text></a><polygon points="314,29 318,29 318,15 314,15" style="fill:black;stroke-width:0"/></svg>

### Grammar
```
create_table ::= CREATE TABLE [ IF NOT EXISTS ] table_name ( [ table_elem [, ...] ] ) [ WITH ( storage_parameter [, ... ] ) | WITHOUT OIDS ]

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

storage_param ::= param_name [ = param_value ]
```

Where

- `qualified_name` and `name` are identifiers (`qualified_name` can be a qualified name).
- `expression` for DEFAULT keyword must be of the same type as the column it modifies. It must be of type boolean for CHECK constraints.
- `param_name` and `param_value` represent storage parameters [as defined by PostgreSQL](https://www.postgresql.org/docs/11/sql-createtable.html#SQL-CREATETABLE-STORAGE-PARAMETERS)

## Semantics
- An error is raised if `qualified_name` already exists in the specified database.
- Storage parameters through the `WITH` clause are accepted [for PostgreSQL compatibility](https://www.postgresql.org/docs/11/sql-createtable.html#SQL-CREATETABLE-STORAGE-PARAMETERS), but they will be ignored. The exceptions are `oids=true` and `user_catalog_table=true`, which are explicitly disallowed and will result in an error being thrown.

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
