---
title: SELECT
summary: Retrieves rows from a table.
---
<style>
table {
  float: left;
}
#psyn {
  text-indent: 50px;
}
#psyn2 {
  text-indent: 100px;
}
#ptodo {
  color: red
}
</style>

## Synopsis
`SELECT` retrieves rows of specified columns from a given table that meet a given condition. For example, the following select command selects all rows of data for all columns in the table `yugatab`.
<p id=psyn>`SELECT * FROM yugatab;`</p>

## Syntax
select::=
<p id=psyn>
   `SELECT [ DISTINCT ] { * | column_name [ column_name ... ] } FROM table_name WHERE where_expression`
</p>

where<br>
  <li>`table_name` and `column_name` are identifier.</li>
  <li>See [Expression](..#expressions) for more information on syntax rules.</li>
</p>

## Semantics
<li>An error is raised if the specified `table_name` does not exist.</li>
<li>The `where_expression` and `if_expression` must be resulted in boolean values.</li>
<li>The `where_expression` can only use `AND` and comparison operators. Other operators are not yet supported.</li>
<li>The `where_expression` can specify conditions for one or more primary-key columns. Only '=' operator can be used for the comparison on partitioning columns. Only operators '=', '<', '<=', '>', and '>=' can be used for the comparison on range columns.</li>
<li>The `where_expression` must not specify conditions on any regular columns.</li>
<li>When there `where_expression` does not specified conditions on one or more partitioning columns, a table scan will occurs.</li>
<li> `SELECT DISTINCT` can only be used for primary-key or static columns.</li>

## Examples

cqlsh>`SELECT * FROM yugatab WHERE id = 7;`<br>

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[`Expression`](..#expressions)
[Other SQL Statements](..)
