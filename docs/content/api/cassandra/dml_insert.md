---
title: INSERT
summary: Add a new row to a table
weight: 1200
---

## Synopsis

The `INSERT` statement adds a row to a specified table.

## Syntax
### Diagram
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="743" height="160" viewbox="0 0 743 160"><path class="connector" d="M0 52h5m65 0h10m50 0h10m91 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h10m68 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h34m24 0h35q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h5m-743 35h25m32 0h50m45 0h20m-80 0q5 0 5 5v8q0 5 5 5h55q5 0 5-5v-8q0-5 5-5m5 0h10m64 0h20m-194 0q5 0 5 5v35q0 5 5 5h5m98 0h66q5 0 5-5v-35q0-5 5-5m5 0h20m-276 0q5 0 5 5v53q0 5 5 5h251q5 0 5-5v-53q0-5 5-5m5 0h30m60 0h10m41 0h10m104 0h20m-260 0q5 0 5 5v8q0 5 5 5h235q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="35" width="65" height="25" rx="7"/><text class="text" x="15" y="52">INSERT</text><rect class="literal" x="80" y="35" width="50" height="25" rx="7"/><text class="text" x="90" y="52">INTO</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="140" y="35" width="91" height="25"/><text class="text" x="150" y="52">table_name</text></a><rect class="literal" x="241" y="35" width="25" height="25" rx="7"/><text class="text" x="251" y="52">(</text><rect class="literal" x="337" y="5" width="24" height="25" rx="7"/><text class="text" x="347" y="22">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="296" y="35" width="106" height="25"/><text class="text" x="306" y="52">column_name</text></a><rect class="literal" x="432" y="35" width="25" height="25" rx="7"/><text class="text" x="442" y="52">)</text><rect class="literal" x="467" y="35" width="68" height="25" rx="7"/><text class="text" x="477" y="52">VALUES</text><rect class="literal" x="545" y="35" width="25" height="25" rx="7"/><text class="text" x="555" y="52">(</text><rect class="literal" x="629" y="5" width="24" height="25" rx="7"/><text class="text" x="639" y="22">,</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="600" y="35" width="83" height="25"/><text class="text" x="610" y="52">expression</text></a><rect class="literal" x="713" y="35" width="25" height="25" rx="7"/><text class="text" x="723" y="52">)</text><rect class="literal" x="25" y="70" width="32" height="25" rx="7"/><text class="text" x="35" y="87">IF</text><rect class="literal" x="107" y="70" width="45" height="25" rx="7"/><text class="text" x="117" y="87">NOT</text><rect class="literal" x="182" y="70" width="64" height="25" rx="7"/><text class="text" x="192" y="87">EXISTS</text><a xlink:href="../grammar_diagrams#if-expression"><rect class="rule" x="87" y="115" width="98" height="25"/><text class="text" x="97" y="132">if_expression</text></a><rect class="literal" x="316" y="70" width="60" height="25" rx="7"/><text class="text" x="326" y="87">USING</text><rect class="literal" x="386" y="70" width="41" height="25" rx="7"/><text class="text" x="396" y="87">TTL</text><a xlink:href="../grammar_diagrams#ttl-expression"><rect class="rule" x="437" y="70" width="104" height="25"/><text class="text" x="447" y="87">ttl_expression</text></a></svg>

### Grammar

```
insert ::= INSERT INTO table_name '(' column [ ',' column ... ] ')'
               VALUES '(' value [ ',' value ... ] ')'
               [ IF { [ NOT ] EXISTS | if_expression } ]
               [ USING TTL ttl_expression ];
```

Where

- `table_name` and `column` are identifiers (`table_name` may be qualified with a keyspace name).
- `value` can be any expression although Apache Cassandra requires that `value`s must be literals.
- Restrictions for `if_expression` and `ttl_expression` are covered in the Semantics section below.
- See [Expressions](..#expressions) for more information on syntax rules.

## Semantics
 - An error is raised if the specified `table_name` does not exist. 
 - The columns list must include all primary key columns.

### `VALUES` Clause
 - The values list must have the same length as the columns list.
 - Each value must be convertible to its corresponding (by position) column type.
 - Each value literal can be an expression that evaluates to a simple value.

### `IF` Clause
 - The `if_expression` can only apply to non-key columns (regular columns).
 - The `if_expression` can contain any logical and boolean operators.

### `USING` Clause
 - `ttl_expression` must be an integer value (or a bind variable marker for prepared statements).

## Examples

### Insert a row into a table
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
cqlsh:example> SELECT * FROM employees;
```
```
 department_id | employee_id | name
---------------+-------------+------
             1 |           1 | John
             1 |           2 | Jane

```

### Conditional insert using the `IF` clause

```{.sql .copy .separator-gt} 
cqlsh:example> INSERT INTO employees(department_id, employee_id, name) VALUES (2, 1, 'Joe') IF name = null;
```
```
 [applied]
-----------
      True
```
```{.sql .copy .separator-gt} 
cqlsh:example> INSERT INTO employees(department_id, employee_id, name) VALUES (2, 1, 'Jack') IF NOT EXISTS;
```
``` 
 [applied]
-----------
     False
cqlsh:example> SELECT * FROM employees;

 department_id | employee_id | name
---------------+-------------+------
             2 |           1 |  Joe
             1 |           1 | John
             1 |           2 | Jane
```

### Insert a row with expiration time using the `USING TTL` clause

```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO employees(department_id, employee_id, name) VALUES (2, 2, 'Jack') USING TTL 10;
```
```{.sql .copy .separator-gt} 
cqlsh:example> SELECT * FROM employees;
```
```
 department_id | employee_id | name
---------------+-------------+------
             2 |           1 |  Joe
             2 |           2 | Jack
             1 |           1 | John
             1 |           2 | Jane
```
```{.sql .copy .separator-gt}              
cqlsh:example> SELECT * FROM employees; -- 11 seconds after the insert. 
```
``` 
 department_id | employee_id | name
---------------+-------------+------
             2 |           1 |  Joe
             1 |           1 | John
             1 |           2 | Jane
```

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`DELETE`](../dml_delete)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[`Expression`](..#expressions)
[Other CQL Statements](..)
