---
title: INSERT
summary: Add a new row to a table.
---
<style>
table {
  float: left;
}
#ptodo {
  color: red
}
</style>

## Synopsis
`INSERT` adds a row to a specified table. Currently, YugaByte can only insert one row at a time. Inserting multiple rows is not yet supported. For example, the following insert command adds a new row to the table `yugatab` with the given values.

`INSERT INTO yugatab(id, name) VALUES(7, 'Joe');`

## Syntax
```
insert ::= INSERT INTO table_name '(' column [, column ... ] ')'
               VALUES '(' value [, value ... ] ')'
               [ IF { [ NOT ] EXISTS | if_expression } ]
               [ USING TTL ttl_expression ];
```

where<br>
  <li>`table_name` and `column` is an identifier.</li>
  <li>`value` can be any expression although Apache Cassandra restricts that `value` must be literals.</li>
  <li>See [Expression](..#expressions) for more information on syntax rules.</li>
  <li>See Semantics Section for restrictions of `ttl_expression`, `where_expression`, and `if_expression`.</li>

## Semantics
<li>An error is raised if the specified `table_name` does not exist.</li>
<li>The `if_expression` can only apply to non-key columns (regular columns).</li>
<li>The `if_expression` can contain any logical and boolean operators.</li>

## Examples
``` sql
cqlsh:yugaspace> INSERT INTO yugatab(id, name) VALUES(7, 'Joe');
```

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`DELETE`](../dml_delete)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[`Expression`](..#expressions)
[Other SQL Statements](..)
