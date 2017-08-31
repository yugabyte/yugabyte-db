---
title: ALTER TABLE
summary: Change the schema of a table. 
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
`ALTER TABLE` command is to change the schema or definition of an existing table.

## Syntax
alter_table::=
<p id=psyn><code>
  ALTER TABLE table_name alter_operator [ alter_operator ...]
</code></p>

alter_operator::=
<p id=psyn><code>
{ add_op | drop_op | rename_op | alter_property_op }
</code></p>

add_op::=
<p id=psyn><code>
   ADD column_name column_type [ column_name column_type ...]
</code></p>

drop_op::=
<p id=psyn><code>
   DROP column_name [ column_name ...]
</code></p>

rename_op::=
<p id=psyn><code>
   RENAME column_name TO column_name [ column_name TO column_name ... ]
</code></p>

alter_property_op::=
<p id=psyn><code>
   WITH property_name = property_literal [ property_name = property_literal ... ]
</code></p>

Where<br>
  <li>`table_name`, `column_name`, and `property_name` are identifiers.</li>
  <li>`property_literal` be a literal of either boolean, text, or map datatype.</li>
</p>

## Semantics
<li>An error is raised if `table_name` does not exists in the associate keyspace.</li>
<li>Columns that are part of `PRIMARY KEY` can be not be altered</li>

## Examples

cqlsh:yugaspace>`CREATE TABLE yugatab (id INT, name TEXT, salary FLOAT, PRIMARY KEY((id), name))`;<br>

cqlsh:yugaspace>`ALTER TABLE yugatab ADD title TEXT`;<br>

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`DELETE`](../dml_delete)
[`DROP TABLE`](../ddl_drop_table)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[Other SQL Statements](..)
