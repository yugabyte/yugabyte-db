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
``` sql
cqlsh:yugaspace> CREATE TABLE yugatab (id INT, name TEXT, salary FLOAT, PRIMARY KEY((id), name));
cqlsh:yugaspace> ALTER TABLE yugatab ADD title TEXT;
```

## See Also

[`CREATE TABLE`](../ddl_create_table)
[`DELETE`](../dml_delete)
[`DROP TABLE`](../ddl_drop_table)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[Other SQL Statements](..)
