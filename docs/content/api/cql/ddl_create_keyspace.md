---
title: CREATE KEYSPACE
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
`CREATE KEYSPACE` command is to create an abstract container for database objects. For example, the following command creates a new keyspace with the name "yugaspace".
<p id=psyn>`CREATE KEYSPACE yugaspace;`</p>

## Syntax
create_keyspace::=
<p id=psyn><code>
   CREATE { KEYSPACE | SCHEMA } [ IF NOT EXIST ] keyspace_name [ keyspace_property [, keyspace_property ...]];
</code></p>

keyspace_property::=
<p id=psyn><code>
property_name = property_value<br></code>
</code></p>
Where<br>
  <li>`keyspace_name` and `property_name` are identifiers.</li>
  <li><code>property_value</code> must be a literal of either boolean, text, or map datatype.</li>
</p>

## Semantics

<li>The `keyspace_name` must be a unique name among all keyspaces in the entire system.</li>
<li>An error is raised if the specified keyspace already exists unless `IF NOT EXISTS` option is present.</li>

## Examples

cqlsh>`CREATE KEYSPACE yugaspace;`<br>

cqlsh> `DESCRIBE KEYSPACES;`<br>
yugaspace  system_schema  system  default_keyspace<br>

cqlsh> `CREATE DATABASE yugaspace;`<br>
InvalidRequest: Error from server: code=2200 [Invalid query] message="SQL error (yb/sql/ptree/process_context.cc:41): Keyspace Already Exists - Already present (yb/common/wire_protocol.cc:130): Namespace yugaspace already exists: 346b3b58aa2c4a23b9e3b9552c4f5931<br>
create keyspace yugaspace;<br>
^^^^^^<br>

## See Also
[`DROP KEYSPACE`](../ddl_drop_keyspace)
[`USE`](../ddl_use)
[Other SQL Statements](..)
