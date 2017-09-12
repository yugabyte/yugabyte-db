---
title: DELETE
summary: Deletes rows from a table.
---

## Synopsis
The `DELETE` statement removes rows from a specified table that meet a given condition. Currently, YugaByte can deletes one row at a time. Deleting multiple rows is not yet supported.

## Syntax
### Diagram
<svg version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="746" height="95" viewbox="0 0 746 95"><defs><style type="text/css">.c{fill:none;stroke:#222222;}.j{fill:#000000;font-family:Verdana,Sans-serif;font-size:12px;}.l{fill:#90d9ff;stroke:#222222;}.r{fill:#d3f0ff;stroke:#222222;}</style></defs><path class="c" d="M0 22h5m67 0h10m54 0h10m91 0h10m65 0h10m128 0h30m32 0h50m45 0h20m-80 0q5 0 5 5v8q0 5 5 5h55q5 0 5-5v-8q0-5 5-5m5 0h10m64 0h20m-194 0q5 0 5 5v35q0 5 5 5h5m98 0h66q5 0 5-5v-35q0-5 5-5m5 0h20m-276 0q5 0 5 5v53q0 5 5 5h251q5 0 5-5v-53q0-5 5-5m5 0h5"/><rect class="l" x="5" y="5" width="67" height="25" rx="7"/><text class="j" x="15" y="22">DELETE</text><rect class="l" x="82" y="5" width="54" height="25" rx="7"/><text class="j" x="92" y="22">FROM</text><a xlink:href="#table_name"><rect class="r" x="146" y="5" width="91" height="25"/><text class="j" x="156" y="22">table_name</text></a><rect class="l" x="247" y="5" width="65" height="25" rx="7"/><text class="j" x="257" y="22">WHERE</text><a xlink:href="#where_expression"><rect class="r" x="322" y="5" width="128" height="25"/><text class="j" x="332" y="22">where_expression</text></a><rect class="l" x="480" y="5" width="32" height="25" rx="7"/><text class="j" x="490" y="22">IF</text><rect class="l" x="562" y="5" width="45" height="25" rx="7"/><text class="j" x="572" y="22">NOT</text><rect class="l" x="637" y="5" width="64" height="25" rx="7"/><text class="j" x="647" y="22">EXISTS</text><a xlink:href="#if_expression"><rect class="r" x="542" y="50" width="98" height="25"/><text class="j" x="552" y="67">if_expression</text></a></svg>

### Grammar
```
delete ::= DELETE FROM table_name
               WHERE where_expression
               [ IF { [ NOT ] EXISTS | if_expression } ];
```
Where

- `table_name` is an identifier (possibly qualified with a keyspace name).
- Restrictions on `where_expression` and `if_expression` are covered in the Semantics section below.
- See [Expressions](..#expressions) for more information on syntax rules.

## Semantics

 - An error is raised if the specified `table_name` does not exist.
 - The `where_expression` and `if_expression` must evaluate to [boolean](../type_bool) values.

### `WHERE` Clause

 - The `where_expression` must specify conditions for all primary-key columns.
 - The `where_expression` must not specifiy conditions for any regular columns.
 - The `where_expression` can only apply `AND` and `=` operators. Other operators are not yet supported.
 
### `IF` Clause

 - The `if_expression` can only apply to non-key columns (regular columns).
 - The `if_expression` can contain any logical and boolean operators.
 - Deleting only some column values from a row is not yet supported.
 - `IF EXISTS` and `IF NOT EXISTS` options are mostly for symmetry with the [`INSERT`](../dml_insert) and [`UPDATE`](dml_update) statements
   - `IF EXISTS` works like a normal delete but additionally returns whether the delete was applied (a row was found with that primary key).
   - `IF NOT EXISTS` is effectively a no-op since rows that do not exist cannot be deleted (but returns whether no row was found with that primary key).

## Examples

### Delete a row from a table

``` sql
cqlsh:example> CREATE TABLE employees(department_id INT, 
                                      employee_id INT, 
                                      name TEXT, 
                                      PRIMARY KEY(department_id, employee_id));
cqlsh:example> INSERT INTO employees(department_id, employee_id, name) VALUES (1, 1, 'John');
cqlsh:example> INSERT INTO employees(department_id, employee_id, name) VALUES (1, 2, 'Jane');
cqlsh:example> INSERT INTO employees(department_id, employee_id, name) VALUES (2, 1, 'Joe');
cqlsh:example> SELECT * FROM employees;

 department_id | employee_id | name
---------------+-------------+------
             2 |           1 |  Joe
             1 |           1 | John
             1 |           2 | Jane
             
cqlsh:example> -- Delete statements identify rows by the primary key columns.
cqlsh:example> DELETE FROM employees WHERE department_id = 1 AND employee_id = 1;
cqlsh:example> -- Deletes on non-existent rows are no-ops.
cqlsh:example> DELETE FROM employees WHERE department_id = 3 AND employee_id = 1;
cqlsh:example> SELECT * FROM employees;

 department_id | employee_id | name
---------------+-------------+------
             2 |           1 |  Joe
             1 |           2 | Jane
```
### Conditional delete using the `IF` clause

``` sql
cqlsh:example> -- 'IF' clause conditions will return whether they were applied or not.
cqlsh:example> DELETE FROM employees WHERE department_id = 2 AND employee_id = 1 IF name = 'Joe';

 [applied]
-----------
      True
cqlsh:example> DELETE FROM employees WHERE department_id = 3 AND employee_id = 1 IF EXISTS;

 [applied]
-----------
     False

cqlsh:example> SELECT * FROM employees;

 department_id | employee_id | name
---------------+-------------+------
             1 |           2 | Jane
```

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[`Expression`](..#expressions)
[Other CQL Statements](..)
