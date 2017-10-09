---
title: ALTER TABLE
summary: Change the schema of a table. 
---

## Synopsis
The `ALTER TABLE` statement changes the schema or definition of an existing table.
It allows adding, dropping, or renaming a column as well as updating a table property.

## Syntax

### Diagram 
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="676" height="140" viewbox="0 0 676 140"><path class="connector" d="M0 37h5m58 0h10m58 0h10m91 0h30m-5 0q-5 0-5-5v-17q0-5 5-5h399q5 0 5 5v17q0 5-5 5m-394 0h20m46 0h10m106 0h10m98 0h99m-379 25q0 5 5 5h5m53 0h10m106 0h185q5 0 5-5m-369 30q0 5 5 5h5m71 0h10m106 0h10m36 0h10m106 0h5q5 0 5-5m-374-55q5 0 5 5v80q0 5 5 5h5m53 0h10m112 0h10m30 0h10m111 0h18q5 0 5-5v-80q0-5 5-5m5 0h25"/><rect class="literal" x="5" y="20" width="58" height="25" rx="7"/><text class="text" x="15" y="37">ALTER</text><rect class="literal" x="73" y="20" width="58" height="25" rx="7"/><text class="text" x="83" y="37">TABLE</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="141" y="20" width="91" height="25"/><text class="text" x="151" y="37">table_name</text></a><rect class="literal" x="282" y="20" width="46" height="25" rx="7"/><text class="text" x="292" y="37">ADD</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="338" y="20" width="106" height="25"/><text class="text" x="348" y="37">column_name</text></a><a xlink:href="../grammar_diagrams#column-type"><rect class="rule" x="454" y="20" width="98" height="25"/><text class="text" x="464" y="37">column_type</text></a><rect class="literal" x="282" y="50" width="53" height="25" rx="7"/><text class="text" x="292" y="67">DROP</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="345" y="50" width="106" height="25"/><text class="text" x="355" y="67">column_name</text></a><rect class="literal" x="282" y="80" width="71" height="25" rx="7"/><text class="text" x="292" y="97">RENAME</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="363" y="80" width="106" height="25"/><text class="text" x="373" y="97">column_name</text></a><rect class="literal" x="479" y="80" width="36" height="25" rx="7"/><text class="text" x="489" y="97">TO</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="525" y="80" width="106" height="25"/><text class="text" x="535" y="97">column_name</text></a><rect class="literal" x="282" y="110" width="53" height="25" rx="7"/><text class="text" x="292" y="127">WITH</text><a xlink:href="../grammar_diagrams#property-name"><rect class="rule" x="345" y="110" width="112" height="25"/><text class="text" x="355" y="127">property_name</text></a><rect class="literal" x="467" y="110" width="30" height="25" rx="7"/><text class="text" x="477" y="127">=</text><a xlink:href="../grammar_diagrams#property-literal"><rect class="rule" x="507" y="110" width="111" height="25"/><text class="text" x="517" y="127">property_literal</text></a></svg>

### Grammar 
```
alter_table ::= ALTER TABLE table_name alter_operator [ alter_operator ...]

alter_operator ::= add_op | drop_op | rename_op | alter_property_op

add_op ::= ADD column_name column_type

drop_op ::= DROP column_name

rename_op ::= RENAME column_name TO column_name

alter_property_op ::= WITH property_name '=' property_literal
```


Where

- `table_name`, `column_name`, and `property_name` are identifiers (`table_name` may be qualified with a keyspace name).
- `property_literal` is a literal of either [boolean](../type_bool), [text](../type_text), or [map](../type_collection) datatype.

## Semantics
- An error is raised if `table_name` does not exists in the associated keyspace.
- Columns that are part of `PRIMARY KEY` cannot be be altered.
- When adding a column its value for all existing rows in the table defaults to `null`.
- After dropping a column all values currently stored for that column in the table are discarded (if any).

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

### Update a table property

``` sql
cqlsh:example> ALTER TABLE employees WITH default_time_to_live = 5;
cqlsh:example> DESCRIBE TABLE employees;

CREATE TABLE example.employees (
    id int,
    name text,
    job_title text,
    PRIMARY KEY (id, name)
) WITH CLUSTERING ORDER BY (name ASC)
    AND default_time_to_live = 5;
```


## See Also

[`CREATE TABLE`](../ddl_create_table)
[`DELETE`](../dml_delete)
[`DROP TABLE`](../ddl_drop_table)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[Other CQL Statements](..)
