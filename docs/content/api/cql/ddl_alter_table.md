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
```
alter_table ::= ALTER TABLE table_name alter_operator [ alter_operator ...]

alter_operator ::= { add_op | drop_op | rename_op | alter_property_op }

add_op ::= ADD column_name column_type [ column_name column_type ...]

drop_op ::= DROP column_name [ column_name ...]

rename_op ::= RENAME column_name TO column_name [ column_name TO column_name ... ]

alter_property_op ::= WITH property_name = property_literal [ property_name = property_literal ... ]
```
Where
  <li>`table_name`, `column_name`, and `property_name` are identifiers.</li>
  <li>`property_literal` be a literal of either boolean, text, or map datatype.</li>

## Semantics
<li>An error is raised if `table_name` does not exists in the associate keyspace.</li>
<li>Columns that are part of `PRIMARY KEY` can be not be altered</li>

## Examples

### Add a column to a table

``` sql
cqlsh:example> CREATE TABLE employees (id INT, name TEXT, salary FLOAT, PRIMARY KEY((id), name));
cqlsh:example> -- Add a column 'title' of type 'TEXT'.
cqlsh:example> ALTER TABLE employees ADD title TEXT;
cqlsh:example> DESCRIBE TABLE employees;

CREATE TABLE example.employees (
    id int,
    name text,
    salary float,
    title text,
    PRIMARY KEY (id, name)
) WITH CLUSTERING ORDER BY (name ASC);
```

### Remove a column from a table

``` sql
cqlsh:example> -- Remove the 'salary' column.
cqlsh:example> ALTER TABLE employees DROP salary;
cqlsh:example> DESCRIBE TABLE employees;

CREATE TABLE example.employees (
    id int,
    name text,
    title text,
    PRIMARY KEY (id, name)
) WITH CLUSTERING ORDER BY (name ASC);
```

### Rename a column in a table

``` sql
cqlsh:example> ALTER TABLE employees RENAME title TO job_title;
cqlsh:example> DESCRIBE TABLE employees;

CREATE TABLE example.employees (
    id int,
    name text,
    job_title text,
    PRIMARY KEY (id, name)
) WITH CLUSTERING ORDER BY (name ASC);
```

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`DELETE`](../dml_delete)
[`DROP TABLE`](../ddl_drop_table)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[Other SQL Statements](..)
