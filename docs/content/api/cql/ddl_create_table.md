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
create_table::=
<p id=psyn><code>
   CREATE TABLE [ IF NOT EXIST ] table_name '(' table_element [, table_element] ')' [ table_option [AND table_option]];
</code></p>

table_element::=
<p id=psyn><code>
{ table_column | table_constraints }
</code></p>

table_column::=
<p id=psyn><code>
   column_name column_type [ column_constraint [ column_constraint ...] ]
</code></p>

column_constraint::=
<p id=psyn><code>
   { STATIC | PRIMARY KEY }
</code></p>

table_constraints::=
<p id=psyn><code>
   PRIMARY KEY '(' [ '(' constraint_column_list ')' ] constraint_column_list ')'
</code></p>

constraint_column_list::=
<p id=psyn><code>
   column_name [, column_name ...]
</code></p>

table_option::=
<p id=psyn><code>
   WITH table_property [ AND table_property ...]
</code></p>

table_property::=
<p id=psyn><code>
   { property_name = property_literal | CLUSTERING ORDER BY '(' clustering_column_list ')' | COMPACT STORAGE }
</code></p>

clustering_column_list::=
<p id=psyn><code>
   clustering_column [, clustering_column ]
</code></p>

clustering_column::=
<p id=psyn><code>
   column_name [ { ASC | DESC } ]
</code></p>

Where<br>
  <li>`table_name`, `column_name`, and `property_name` are identifiers.</li>
  <li>`property_literal` be a literal of either boolean, text, or map datatype.</li>
</p>

## Semantics
<li>An error is raised if `table_name` already exists in the associate keyspace unless `IF NOT EXISTS` is present.</li>
<li>`PRIMARY KEY` can be defined in either `column_constraint` or `table_constraint` but not both of them</li>
<li>In the `PRIMARY KEY` specification, the optional nested `constraint_column_list` is the partition columns. When this option is not used, the first column in the required `constraint_column_list` is the partition column.</li>

## Examples

cqlsh:yugaspace>`CREATE TABLE simple(id int primary key);`<br>
<i>The above statement uses column constraint to define primary key</i><br>

cqlsh:yugaspace>`CREATE TABLE yugatab (id int, name text, salary double, primary key((id), name))`;<br>
<i>The above statement uses table constraint to define primary key where the nested column list `(id)` is the partitioning column while `name` is the range column</i><br>

## See Also

[`ALTER TABLE`](../ddl_alter_table)
[`DELETE`](../dml_delete)
[`DROP TABLE`](../ddl_drop_table)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[Other SQL Statements](..)
