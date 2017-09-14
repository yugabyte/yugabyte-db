---
title: DROP KEYSPACE
summary: Removes a keyspace and all of its database objects.
---

## Synopsis
The `DROP KEYSPACE` statement removes a keyspace and all its database objects (such as [tables](../ddl_create_table) or [types](../ddl_create_type)) from the system.

## Syntax

### Diagram

<svg version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="477" height="65" viewbox="0 0 477 65"><defs><style type="text/css">.c{fill:none;stroke:#222222;}.j{fill:#000000;font-family:Verdana,Sans-serif;font-size:12px;}.l{fill:#90d9ff;stroke:#222222;}.r{fill:#d3f0ff;stroke:#222222;}</style></defs><path class="c" d="M0 22h5m53 0h30m82 0h20m-117 0q5 0 5 5v20q0 5 5 5h5m71 0h16q5 0 5-5v-20q0-5 5-5m5 0h30m32 0h10m64 0h20m-141 0q5 0 5 5v8q0 5 5 5h116q5 0 5-5v-8q0-5 5-5m5 0h10m116 0h5"/><rect class="l" x="5" y="5" width="53" height="25" rx="7"/><text class="j" x="15" y="22">DROP</text><rect class="l" x="88" y="5" width="82" height="25" rx="7"/><text class="j" x="98" y="22">KEYSPACE</text><rect class="l" x="88" y="35" width="71" height="25" rx="7"/><text class="j" x="98" y="52">SCHEMA</text><rect class="l" x="220" y="5" width="32" height="25" rx="7"/><text class="j" x="230" y="22">IF</text><rect class="l" x="262" y="5" width="64" height="25" rx="7"/><text class="j" x="272" y="22">EXISTS</text><a xlink:href="#keyspace_name"><rect class="r" x="356" y="5" width="116" height="25"/><text class="j" x="366" y="22">keyspace_name</text></a></svg>

### Grammar

```
drop_keyspace ::= DROP { KEYSPACE | SCHEMA } [ IF EXISTS ] keyspace_name;
```
Where

- `keyspace_name` is an identifier.

## Semantics

- An error is raised if the specified `keyspace_name` does not exist unless `IF EXISTS` option is present.
- An error is raised if the specified keyspace is non-empty (contains tables or types).

## Examples
``` sql
cqlsh> CREATE KEYSPACE example;

cqlsh> DROP KEYSPACE example;

cqlsh> DROP KEYSPACE IF EXISTS example;
```

## See Also
[`CREATE KEYSPACE`](../ddl_create_keyspace)
[`USE`](../ddl_use)
[Other CQL Statements](..)
