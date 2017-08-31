---
title: DROP KEYSPACE
summary: Removes a keyspace and all of its database objects.
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
`DROP KEYSPACE` command is to remove a keyspace and all its database objects from the system. For example, the following command drops the keyspace "yugaspace".
<p id=psyn>`DROP KEYSPACE yugaspace;`</p>

## Syntax
drop_keyspace::=
<p id=psyn><code>
   DROP { KEYSPACE | SCHEMA } [ IF EXISTS ] keyspace_name;
</code></p>

Where<br>
  <li>`keyspace_name` is an identifier.</li>
</p>

## Semantics

<li>An error is raised if the specified `keyspace_name` does not exist unless `IF EXISTS` option is present.</li>

## Examples

cqlsh>`CREATE KEYSPACE yugaspace;`<br>

cqlsh>`DROP KEYSPACE yugaspace;`<br>

## See Also
[`CREATE KEYSPACE`](../ddl_create_keyspace)
[`USE`](../ddl_use)
[Other SQL Statements](..)
