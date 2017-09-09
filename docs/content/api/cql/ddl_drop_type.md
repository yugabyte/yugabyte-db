---
title: DROP TYPE
summary: Drop a user-defined ddatatype
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
`DROP TYPE` command is to remove an existing user-defined datatype. For example, the following command drops the datatype "yugatype".
<p id=psyn>`DROP TYPE yugatype;`</p>

## Syntax
```
drop_type ::= DROP TYPE [ IF EXISTS ] type_name;
```
Where
<li>`type_name` is an identifier.</li>

## Semantics

<li>An error is raised if the specified `type_name` does not exist unless `IF EXISTS` option is present.</li>
<li>A user-defined `type_name` cannot be dropped if it is currently used in a table or another type</li>

## Examples

``` sql
cqlsh:example> CREATE TYPE person(first_name TEXT, last_name TEXT, email TEXT);
cqlsh:example> DROP TYPE person;
```

## See Also
[`CREATE TABLE`](../ddl_create_table)
[`DROP TYPE`](../ddl_drop_keyspace)
[Other SQL Statements](..)
