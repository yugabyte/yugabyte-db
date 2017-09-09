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
```
use_keyspace ::= USE keyspace_name;
```
Where
  <li>`keyspace_name` must be an identifier that cannot be any reserved keyword and cannot contains whitespaces, or it has to be double-quoted.</li>

## Semantics

<li>If the specified keyspace does not exists, an error is raised.</li>

## Examples
### Create and use keyspaces

``` sql
cqlsh> CREATE KEYSPACE example;

cqlsh> CREATE KEYSPACE other_keyspace;

cqlsh> USE example;
```

### Create a table in the current keyspace

``` sql
cqlsh:example> CREATE TABLE test(id INT PRIMARY KEY);
cqlsh:example> INSERT INTO test(id) VALUES (1);
```

### Create a table in another keyspace

``` sql
cqlsh:example> CREATE TABLE other_keyspace.test(id INT PRIMARY KEY);
cqlsh:example> INSERT INTO other_keyspace.test(id) VALUES (2);
```

``` sql
cqlsh:example> SELECT * FROM test;
 id
----
  1

cqlsh:example> SELECT * FROM other_keyspace.test;
 id
----
  2
```

</li>

## See Also
[`CREATE KEYSPACE`](../ddl_create_keyspace)
[`DROP KEYSPACE`](../ddl_drop_keyspace)
[Other CQL Statements](..)
