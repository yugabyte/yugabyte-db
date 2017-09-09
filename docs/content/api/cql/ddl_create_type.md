---
title: CREATE TYPE
summary: Create a new datatype
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
`CREATE TYPE` command is to create a user-defined datatype of one or more fields. For example, the following command defines a new datatype "yugatype" of two fields, `name` of type `TEXT` and `id` of type `INT`.
<p id=psyn>`CREATE TYPE yugatype(name TEXT, id INT);`</p>

## Syntax
```
create_type ::= CREATE TYPE [ IF NOT EXIST ] type_name
                    (field_name field_type [, field_name field_type ...]);
```
Where
  <li>`type_name` and `field_name` are identifiers.</li>

## Semantics

<li>An error is raised if the specified `type_name` already exists unless `IF NOT EXISTS` option is present.</li>
<li>The `type_name` must be a unique name among all user-defined datatypes in the entire keyspace that to which it belongs.</li>
<li>The `field_type` can be either a primitive or user-defined datatype.</li>

## Examples
```
cqlsh:example> CREATE TYPE person(first_name TEXT, last_name TEXT, email TEXT);

cqlsh:yugaspace> DESCRIBE TYPE person;

CREATE TYPE example.person (
    first_name text,
    last_name text,
    email text
);
```

## See Also
[`CREATE TABLE`](../ddl_create_table)
[`DROP TYPE`](../ddl_drop_keyspace)
[Other SQL Statements](..)
