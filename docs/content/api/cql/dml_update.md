---
title: UPDATE
summary: Change values of a row in a table.
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
`UPDATE` removes rows from a specified table that meet a given condition. Currently, YugaByte can only update one row at a time. Updating multiple rows is not yet supported. For example, the following update command changes the value of the column `name` of the row in `yugatab` who has `id = 7`.

`UPDATE yugatab SET name = 'Scott Tiger' WHERE id = 7;`

## Syntax
```
update ::= UPDATE table_name
              [ USING TTL ttl_expression ]
              SET assignment [, assignment ... ]
              WHERE where_expression
              [ IF { [ NOT ] EXISTS | if_expression } ]

assignment ::= { column_name | column_name'['index_expression']' } '=' expression
```
where
  <li>`table_name` is an identifier.</li>
  <li>See [Expression](..#expressions) for more information on syntax rules.</li>
  <li>See Semantics Section for restrictions of `ttl_expression`, `where_expression`, and `if_expression`.</li>

## Semantics
<li>An error is raised if the specified `table_name` does not exist.</li>
<li>The `where_expression` and `if_expression` must be resulted in boolean values.</li>
<li>The `where_expression` must specify conditions for all primary-key columns.</li>
<li>The `where_expression` must not specifiy conditions for any regular columns.</li>
<li>The `where_expression` can only apply `AND` and `=` operators. Other operators are not yet supported.</li>
<li>The `if_expression` can only apply to non-key columns (regular columns).</li>
<li>The `if_expression` can contain any logical and boolean operators.</li>

## Examples

``` sql
cqlsh:example> CREATE TABLE employees(department_id INT, 
                                      employee_id INT, 
                                      name TEXT, 
                                      PRIMARY KEY(department_id, employee_id));
cqlsh:example> INSERT INTO employees(department_id, employee_id, name) values (1, 1, 'John');
cqlsh:example> -- Update the value of a non primary-key column. 
cqlsh:example> UPDATE employees SET name = 'Jack' WHERE department_id = 1 AND employee_id = 1;
cqlsh:example> -- Using upsert semantics to update a non-existent row (i.e. add the row).
cqlsh:example> UPDATE employees SET name = 'Jane' WHERE department_id = 1 AND  employee_id = 2;
cqlsh:example> -- Using IF clause for conditional update.
cqlsh:example> UPDATE employees SET name = 'Joe' WHERE department_id = 2 AND employee_id = 1 IF name = null;

 [applied]
-----------
      True

cqlsh:example> SELECT * FROM employees;

 department_id | employee_id | name
---------------+-------------+------
             2 |           1 |  Joe
             1 |           1 | Jack
             1 |           2 | Jane

```

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`DELETE`](../dml_delete)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[`Expression`](..#expressions)
[Other CQL Statements](..)
