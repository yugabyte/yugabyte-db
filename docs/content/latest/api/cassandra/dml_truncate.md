---
title: TRUNCATE
summary: Removes all rows from a table.
description: TRUNCATE
menu:
  latest:
    parent: api-cassandra
    weight: 1330
aliases:
  - api/cassandra/dml_truncate
  - api/cql/dml_truncate
---

## Synopsis
The `TRUNCATE` statement removes all rows from a specified table.

## Syntax
### Diagram
<svg version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="303" height="50" viewbox="0 0 303 50"><defs><style type="text/css">.c{fill:none;stroke:#222222;}.j{fill:#000000;font-family:Verdana,Sans-serif;font-size:12px;}.l{fill:#90d9ff;stroke:#222222;}.r{fill:#d3f0ff;stroke:#222222;}</style></defs><path class="c" d="M0 22h5m84 0h30m58 0h20m-93 0q5 0 5 5v8q0 5 5 5h68q5 0 5-5v-8q0-5 5-5m5 0h10m91 0h5"/><rect class="l" x="5" y="5" width="84" height="25" rx="7"/><text class="j" x="15" y="22">TRUNCATE</text><rect class="l" x="119" y="5" width="58" height="25" rx="7"/><text class="j" x="129" y="22">TABLE</text><a xlink:href="../grammar_diagrams#table-name"><rect class="r" x="207" y="5" width="91" height="25"/><text class="j" x="217" y="22">table_name</text></a></svg>

### Grammar
```
truncate ::= TRUNCATE [ TABLE ] table_name;
```
Where

- `table_name` is an identifier (possibly qualified with a keyspace name).

## Semantics

 - An error is raised if the specified `table_name` does not exist.

## Examples

### Truncate a table

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
```sh
 department_id | employee_id | name
---------------+-------------+------
             2 |           1 |  Joe
             1 |           1 | John
             1 |           2 | Jane
```             
Remove all rows from the table.
```{.sql .copy .separator-gt}
cqlsh:example> TRUNCATE employees;
```
```{.sql .copy .separator-gt}
cqlsh:example> SELECT * FROM employees;
```
```sh
 department_id | employee_id | name
---------------+-------------+------
```

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[`DELETE`](../dml_delete)
[Other CQL Statements](..)
