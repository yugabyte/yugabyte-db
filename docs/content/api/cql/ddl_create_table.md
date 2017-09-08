---
title: CREATE TABLE
summary: Create a new table in a keyspace.
toc: false
---
<style>
table {
  float: left;
}
#psyn {
  text-indent: 50px;
}
#ptodo {
  color: red
}
</style>

## Synopsis
`CREATE TABLE` creates a new table in a keyspace.

## Syntax
```
create_table ::= CREATE TABLE [ IF NOT EXIST ] table_name '(' table_element [, table_element] ')'
                     [ table_option [AND table_option]];

table_element ::= { table_column | table_constraints }

table_column ::= column_name column_type [ column_constraint [ column_constraint ...] ]

column_constraint ::= { PRIMARY KEY | STATIC }

table_constraints ::= PRIMARY KEY '(' [ '(' constraint_column_list ')' ] constraint_column_list ')'

constraint_column_list ::= column_name [, column_name ...]

table_option ::= WITH table_property [ AND table_property ...]

table_property ::= { property_name = property_literal
                     | CLUSTERING ORDER BY '(' clustering_column_list ')'
                     | COMPACT STORAGE }

clustering_column_list ::= clustering_column [, clustering_column ]

clustering_column ::= column_name [ { ASC | DESC } ]
```
Where
  <li>`table_name`, `column_name`, and `property_name` are identifiers.</li>
  <li>`property_literal` be a literal of either boolean, text, or map datatype.</li>

## Semantics
<li>An error is raised if `table_name` already exists in the associate keyspace unless `IF NOT EXISTS` is present.</li>

### PARTITION KEY

### PRIMARY KEY
<li>`PRIMARY KEY` can be defined in either `column_constraint` or `table_constraint` but not both of them</li>
<li>In the `PRIMARY KEY` specification, the optional nested `constraint_column_list` is the partition columns. When this option is not used, the first column in the required `constraint_column_list` is the partition column.</li>

### STATIC COLUMN

### TABLE PROPERTIES

## Examples
<li>Use column constraint to define primary key.
```
cqlsh:yugaspace> CREATE TABLE simple(id INT PRIMARY KEY);
```
</li>

<li> Use table constraint to define primary key where the nested column list `(id)` is the partitioning column while `name` is the range column
```
cqlsh:yugaspace> CREATE TABLE yugatab (id INT, name TEXT, salary DOUBLE, PRIMARY KEY((id), name));
```
</li>

## See Also

[`ALTER TABLE`](../ddl_alter_table)
[`DELETE`](../dml_delete)
[`DROP TABLE`](../ddl_drop_table)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[Other SQL Statements](..)
