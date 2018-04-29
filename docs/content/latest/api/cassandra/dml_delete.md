---
title: DELETE
summary: Deletes rows from a table.
description: DELETE
menu:
  latest:
    parent: api-cassandra
    weight: 1330
aliases:
  - api/cassandra/ddl_delete
  - api/cql/ddl_delete
  - api/ycql/ddl_delete
---

## Synopsis
The `DELETE` statement removes rows from a specified table that meet a given condition. Currently, YugaByte can deletes one row at a time. Deleting multiple rows is not yet supported.

## Syntax
### Diagram
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="746" height="95" viewbox="0 0 746 95"><path class="connector" d="M0 22h5m67 0h10m54 0h10m91 0h10m65 0h10m128 0h30m32 0h50m45 0h20m-80 0q5 0 5 5v8q0 5 5 5h55q5 0 5-5v-8q0-5 5-5m5 0h10m64 0h20m-194 0q5 0 5 5v35q0 5 5 5h5m98 0h66q5 0 5-5v-35q0-5 5-5m5 0h20m-276 0q5 0 5 5v53q0 5 5 5h251q5 0 5-5v-53q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="67" height="25" rx="7"/><text class="text" x="15" y="22">DELETE</text><rect class="literal" x="82" y="5" width="54" height="25" rx="7"/><text class="text" x="92" y="22">FROM</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="146" y="5" width="91" height="25"/><text class="text" x="156" y="22">table_name</text></a><rect class="literal" x="247" y="5" width="65" height="25" rx="7"/><text class="text" x="257" y="22">WHERE</text><a xlink:href="../grammar_diagrams#where-expression"><rect class="rule" x="322" y="5" width="128" height="25"/><text class="text" x="332" y="22">where_expression</text></a><rect class="literal" x="480" y="5" width="32" height="25" rx="7"/><text class="text" x="490" y="22">IF</text><rect class="literal" x="562" y="5" width="45" height="25" rx="7"/><text class="text" x="572" y="22">NOT</text><rect class="literal" x="637" y="5" width="64" height="25" rx="7"/><text class="text" x="647" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#if-expression"><rect class="rule" x="542" y="50" width="98" height="25"/><text class="text" x="552" y="67">if_expression</text></a></svg>

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

### WHERE Clause

 - The `where_expression` must specify conditions for all primary-key columns.
 - The `where_expression` must not specifiy conditions for any regular columns.
 - The `where_expression` can only apply `AND` and `=` operators. Other operators are not yet supported.
 
### IF Clause

 - The `if_expression` can only apply to non-key columns (regular columns).
 - The `if_expression` can contain any logical and boolean operators.
 - Deleting only some column values from a row is not yet supported.
 - `IF EXISTS` and `IF NOT EXISTS` options are mostly for symmetry with the [`INSERT`](../dml_insert) and [`UPDATE`](dml_update) statements
   - `IF EXISTS` works like a normal delete but additionally returns whether the delete was applied (a row was found with that primary key).
   - `IF NOT EXISTS` is effectively a no-op since rows that do not exist cannot be deleted (but returns whether no row was found with that primary key).

## Examples

### Delete a row from a table

```{.sql .copy .separator-gt} 
cqlsh:example> CREATE TABLE employees(department_id INT, 
                                      employee_id INT, 
                                      name TEXT, 
                                      PRIMARY KEY(department_id, employee_id));
```
```{.sql .copy .separator-gt} 
cqlsh:example> INSERT INTO employees(department_id, employee_id, name) VALUES (1, 1, 'John');
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO employees(department_id, employee_id, name) VALUES (1, 2, 'Jane');
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO employees(department_id, employee_id, name) VALUES (2, 1, 'Joe');
```
```{.sql .copy .separator-gt}
cqlsh:example> SELECT * FROM employees;
```
```
 department_id | employee_id | name
---------------+-------------+------
             1 |           1 | John
             1 |           2 | Jane
             2 |           1 |  Joe
```
Delete statements identify rows by the primary key columns.
```{.sql .copy .separator-gt}
cqlsh:example> DELETE FROM employees WHERE department_id = 1 AND employee_id = 1;
```
Deletes on non-existent rows are no-ops.
```{.sql .copy .separator-gt}
cqlsh:example> DELETE FROM employees WHERE department_id = 3 AND employee_id = 1;
```
```{.sql .copy .separator-gt}
cqlsh:example> SELECT * FROM employees;
```
```
 department_id | employee_id | name
---------------+-------------+------
             1 |           2 | Jane
             2 |           1 |  Joe
```
### Conditional delete using the `IF` clause

'IF' clause conditions will return whether they were applied or not.
```{.sql .copy .separator-gt}
cqlsh:example> DELETE FROM employees WHERE department_id = 2 AND employee_id = 1 IF name = 'Joe';
```
```
 [applied]
-----------
      True
```
```{.sql .copy .separator-gt}
cqlsh:example> DELETE FROM employees WHERE department_id = 3 AND employee_id = 1 IF EXISTS;
```
```
 [applied]
-----------
     False
```
```{.sql .copy .separator-gt}
cqlsh:example> SELECT * FROM employees;
```
```
 department_id | employee_id | name
---------------+-------------+------
             1 |           2 | Jane
```

### Delete several rows with the same partition key

```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO employees(department_id, employee_id, name) VALUES (1, 1, 'John');
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO employees(department_id, employee_id, name) VALUES (2, 1, 'Joe');
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO employees(department_id, employee_id, name) VALUES (2, 2, 'Jack');
```
```{.sql .copy .separator-gt}
cqlsh:example> SELECT * FROM employees;
```
```
 department_id | employee_id | name
---------------+-------------+------
             1 |           1 | John
             1 |           2 | Jane
             2 |           1 |  Joe
             2 |           2 | Jack
```

Delete all entries for a partition key.
```{.sql .copy .separator-gt}
cqlsh:example> DELETE FROM employees WHERE department_id = 1;
```
```{.sql .copy .separator-gt}
cqlsh:example> SELECT * FROM employees;
```
```
 department_id | employee_id | name
---------------+-------------+------
             2 |           1 |  Joe
             2 |           2 | Jack
```
Delete a range of entries within a partition key.
```{.sql .copy .separator-gt}
cqlsh:example> DELETE FROM employees WHERE department_id = 2 AND employee_id >= 2 AND employee_id < 4;
```
```{.sql .copy .separator-gt}
cqlsh:example> SELECT * FROM employees;
```
```
 department_id | employee_id | name
---------------+-------------+------
             2 |           1 |  Joe
```

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[`TRUNCATE`](../dml_truncate)
[`Expression`](..#expressions)
[Other CQL Statements](..)
