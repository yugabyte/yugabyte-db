---
title: USE
summary: Create a new database. 
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
`USE` keyspace command is to specify the default keyspace for the current client session. When a database object name does not identify a keyspace, this default keyspace is used. For example, the following command identifies "yugaspace" as the default keyspace.
<p id=psyn>`USE yugaspace;`</p>

## Syntax
use_keyspace::=
<p id=psyn><code>
   USE keyspace_name;
</code></p>

Where<br>
  <li><code>keyspace_name</code> must be an identifier that cannot be any reserved keyword and cannot contains whitespaces, or it has to be double-quoted.</li>
</p>

## Semantics

<li>If the specified keyspace does not exists, an error is raised.</li>

## Example

cqlsh>`CREATE KEYSPACE yugaspace;`<br>

cqlsh>`CREATE KEYSPACE myspace;`<br>

cqlsh> `USE yugaspace`<br>

<i>Create a table in the default keyspace, "yugaspace".</i><br>
cqlsh:yugaspace> `CREATE TABLE yugatab(id int primary key);`<br>

<i>Create a table in "myspace".</i><br>
cqlsh:yugaspace> `CREATE TABLE myspace.yugatab(id int primary key);`<br>

## See Also
[`CREATE KEYSPACE`](../ddl_create_keyspace)
[`DROP KEYSPACE`](../ddl_drop_keyspace)
[Other SQL Statements](..)
