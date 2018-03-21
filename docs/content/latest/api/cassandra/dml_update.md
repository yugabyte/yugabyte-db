---
title: UPDATE
summary: Change values of a row in a table
description: UPDATE
menu:
  latest:
    parent: api-cassandra
    weight: 1320
aliases:
  - api/cassandra/dml_update
  - api/cql/dml_update
---

## Synopsis

The `UPDATE` statement updates one or more column values for a row in table. Currently, YugaByte can only update one row at a time, updating multiple rows is not yet supported.

## Syntax

### Diagram
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="646" height="175" viewbox="0 0 646 175"><path class="connector" d="M0 52h5m68 0h10m91 0h30m60 0h10m41 0h10m104 0h20m-260 0q5 0 5 5v8q0 5 5 5h235q5 0 5-5v-8q0-5 5-5m5 0h10m43 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h37m24 0h38q5 0 5 5v20q0 5-5 5m-5 0h25m-646 50h5m65 0h10m128 0h30m32 0h50m45 0h20m-80 0q5 0 5 5v8q0 5 5 5h55q5 0 5-5v-8q0-5 5-5m5 0h10m64 0h20m-194 0q5 0 5 5v35q0 5 5 5h5m98 0h66q5 0 5-5v-35q0-5 5-5m5 0h20m-276 0q5 0 5 5v53q0 5 5 5h251q5 0 5-5v-53q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="35" width="68" height="25" rx="7"/><text class="text" x="15" y="52">UPDATE</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="83" y="35" width="91" height="25"/><text class="text" x="93" y="52">table_name</text></a><rect class="literal" x="204" y="35" width="60" height="25" rx="7"/><text class="text" x="214" y="52">USING</text><rect class="literal" x="274" y="35" width="41" height="25" rx="7"/><text class="text" x="284" y="52">TTL</text><a xlink:href="../grammar_diagrams#ttl-expression"><rect class="rule" x="325" y="35" width="104" height="25"/><text class="text" x="335" y="52">ttl_expression</text></a><rect class="literal" x="459" y="35" width="43" height="25" rx="7"/><text class="text" x="469" y="52">SET</text><rect class="literal" x="564" y="5" width="24" height="25" rx="7"/><text class="text" x="574" y="22">,</text><a xlink:href="../grammar_diagrams#assignment"><rect class="rule" x="532" y="35" width="89" height="25"/><text class="text" x="542" y="52">assignment</text></a><rect class="literal" x="5" y="85" width="65" height="25" rx="7"/><text class="text" x="15" y="102">WHERE</text><a xlink:href="../grammar_diagrams#where-expression"><rect class="rule" x="80" y="85" width="128" height="25"/><text class="text" x="90" y="102">where_expression</text></a><rect class="literal" x="238" y="85" width="32" height="25" rx="7"/><text class="text" x="248" y="102">IF</text><rect class="literal" x="320" y="85" width="45" height="25" rx="7"/><text class="text" x="330" y="102">NOT</text><rect class="literal" x="395" y="85" width="64" height="25" rx="7"/><text class="text" x="405" y="102">EXISTS</text><a xlink:href="../grammar_diagrams#if-expression"><rect class="rule" x="300" y="130" width="98" height="25"/><text class="text" x="310" y="147">if_expression</text></a></svg>

### Grammar
```
update ::= UPDATE table_name
              [ USING TTL ttl_expression ]
              SET assignment [, assignment ...]
              WHERE where_expression
              [ IF { [ NOT ] EXISTS | if_expression } ]

assignment ::= { column_name | column_name'['index_expression']' } '=' expression
```

Where

- `table_name` is an identifier (possibly qualified with a keyspace name).
- Restrictions for `ttl_expression`, `where_expression`, and `if_expression` are covered in the Semantics section below.
- See [Expressions](..#expressions) for more information on syntax rules.

## Semantics
 - An error is raised if the specified `table_name` does not exist.
 - Update statement uses _upsert semantics_, meaning it inserts the row being updated if it does not already exists.

`WHERE` Clause

 - The `where_expression` and `if_expression` must evaluate to boolean values.
 - The `where_expression` must specify conditions for all primary-key columns.
 - The `where_expression` must not specifiy conditions for any regular columns.
 - The `where_expression` can only apply `AND` and `=` operators. Other operators are not yet supported.
 
 `IF` Clause

 - The `if_expression` can only apply to non-key columns (regular columns).
 - The `if_expression` can contain any logical and boolean operators.

## Examples

### Update a value in a table

```{.sql .copy .separator-gt}
cqlsh:example> CREATE TABLE employees(department_id INT, 
                                      employee_id INT, 
                                      name TEXT, 
                                      age INT, 
                                      PRIMARY KEY(department_id, employee_id));
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO employees(department_id, employee_id, name, age) VALUES (1, 1, 'John', 30);
```
Update the value of a non primary-key column.
```{.sql .copy .separator-gt} 
cqlsh:example> UPDATE employees SET name = 'Jack' WHERE department_id = 1 AND employee_id = 1;
```
Using upsert semantics to update a non-existent row (i.e. insert the row).
```{.sql .copy .separator-gt} 
cqlsh:example> UPDATE employees SET name = 'Jane', age = 40 WHERE department_id = 1 AND employee_id = 2;
```
```{.sql .copy .separator-gt} 
cqlsh:example> SELECT * FROM employees;
```
```sh
 department_id | employee_id | name | age
---------------+-------------+------+-----
             1 |           1 | Jack |  30
             1 |           2 | Jane |  40

```

### Conditional update using the `IF` clause


The supported expressions are allowed in the 'SET' assignment targets.
```{.sql .copy .separator-gt} 
cqlsh:example> UPDATE employees SET age = age + 1 WHERE department_id = 1 AND employee_id = 1 IF name = 'Jack';
```
```sh
 [applied]
-----------
      True
```
Using upsert semantics to add a row, age is not set so will be 'null'.
```{.sql .copy .separator-gt} 
cqlsh:example> UPDATE employees SET name = 'Joe' WHERE department_id = 2 AND employee_id = 1 IF NOT EXISTS;
```
```sh
 [applied]
-----------
      True
```
```{.sql .copy .separator-gt} 
cqlsh:example> SELECT * FROM employees;
```
```sh
 department_id | employee_id | name | age
---------------+-------------+------+------
             2 |           1 |  Joe | null
             1 |           1 | Jack |   31
             1 |           2 | Jane |   40
```

### Update with expiration time using the `USING TTL` clause.

The updated value(s) will persist for the TTL duration.
```{.sql .copy .separator-gt} 
cqlsh:example> UPDATE employees USING TTL 10 SET age = 32 WHERE department_id = 1 AND employee_id = 1;
```
```{.sql .copy .separator-gt}
cqlsh:example> SELECT * FROM employees WHERE department_id = 1 AND employee_id = 1;
```
```sh
 department_id | employee_id | name | age
---------------+-------------+------+------
             1 |           1 | Jack |   32
```
11 seconds after the update (value will have expired).
```{.sql .copy .separator-gt} 
cqlsh:example> SELECT * FROM employees WHERE department_id = 1 AND employee_id = 1;
```
```sh
 department_id | employee_id | name | age
---------------+-------------+------+------
             1 |           1 | Jack | null
```

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`DELETE`](../dml_delete)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[`Expression`](..#expressions)
[Other CQL Statements](..)
