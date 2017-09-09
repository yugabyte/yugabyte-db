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
```
create_keyspace ::= CREATE { KEYSPACE | SCHEMA } [ IF NOT EXIST ] keyspace_name
                       [ keyspace_property [, keyspace_property ...]]

keyspace_property ::= property_name = property_value
```
Where<br>
  <li>`keyspace_name` and `property_name` are identifiers.</li>
  <li>`property_value` must be a literal of either boolean, text, or map datatype.</li>

## Semantics

<li>The `keyspace_name` must be a unique name among all keyspaces in the entire system.</li>
<li>An error is raised if the specified keyspace already exists unless `IF NOT EXISTS` option is present.</li>

## Examples
``` sql
cqlsh> CREATE KEYSPACE example;

cqlsh> DESCRIBE KEYSPACES;
example  system_schema  system  default_keyspace

cqlsh> DESCRIBE example;
CREATE KEYSPACE example WITH replication = {'class': 'SimpleStrategy', 'replication_factor': '3'} AND durable_writes = true;

cqlsh> CREATE SCHEMA example;
SQL error: Keyspace Already Exists
CREATE SCHEMA example;
^^^^^^
```

## See Also
[`DROP KEYSPACE`](../ddl_drop_keyspace)
[`USE`](../ddl_use)
[Other CQL Statements](..)
