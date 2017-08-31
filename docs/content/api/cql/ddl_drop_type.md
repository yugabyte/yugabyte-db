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
drop_type::=
<p id=psyn><code>
   DROP TYPE [ IF EXISTS ] type_name;
</code></p>

Where<br>
  <li>`type_name` is an identifier.</li>
</p>

## Semantics

<li>An error is raised if the specified `type_name` does not exist unless `IF EXISTS` option is present.</li>
<li>A user-defined `type_name` cannot be dropped if it is currently used in a table or another type</li>

## Example

cqlsh:yugaspace>`CREATE TYPE yugatype(name TEXT, id INT);`<br>
cqlsh:yugaspace>`DROP TYPE yugatype;`<br>

## See Also
[`CREATE TABLE`](../ddl_create_table)
[`DROP TYPE`](../ddl_drop_keyspace)
[Other SQL Statements](..)
