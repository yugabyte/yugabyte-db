---
title: CREATE KEYSPACE
summary: Create a new database. 
---

## Synopsis
The `CREATE KEYSPACE` statement is used to create an abstract container for database objects (such as [tables](../ddl_create_table) or [types](../ddl_create_type)). 

## Syntax

### Diagram

#### create_keyspace
<svg version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="697" height="65" viewbox="0 0 697 65"><defs><style type="text/css">.c{fill:none;stroke:#222222;}.j{fill:#000000;font-family:Verdana,Sans-serif;font-size:12px;}.l{fill:#90d9ff;stroke:#222222;}.r{fill:#d3f0ff;stroke:#222222;}</style></defs><path class="c" d="M0 22h5m67 0h30m82 0h20m-117 0q5 0 5 5v20q0 5 5 5h5m71 0h16q5 0 5-5v-20q0-5 5-5m5 0h30m32 0h10m45 0h10m64 0h20m-196 0q5 0 5 5v8q0 5 5 5h171q5 0 5-5v-8q0-5 5-5m5 0h10m116 0h10m141 0h5"/><rect class="l" x="5" y="5" width="67" height="25" rx="7"/><text class="j" x="15" y="22">CREATE</text><rect class="l" x="102" y="5" width="82" height="25" rx="7"/><text class="j" x="112" y="22">KEYSPACE</text><rect class="l" x="102" y="35" width="71" height="25" rx="7"/><text class="j" x="112" y="52">SCHEMA</text><rect class="l" x="234" y="5" width="32" height="25" rx="7"/><text class="j" x="244" y="22">IF</text><rect class="l" x="276" y="5" width="45" height="25" rx="7"/><text class="j" x="286" y="22">NOT</text><rect class="l" x="331" y="5" width="64" height="25" rx="7"/><text class="j" x="341" y="22">EXISTS</text><a xlink:href="#keyspace_name"><rect class="r" x="425" y="5" width="116" height="25"/><text class="j" x="435" y="22">keyspace_name</text></a><a xlink:href="#keyspace_properties"><rect class="r" x="551" y="5" width="141" height="25"/><text class="j" x="561" y="22">keyspace_properties</text></a></svg>

#### keyspace_properties
<svg version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="848" height="80" viewbox="0 0 848 80"><defs><style type="text/css">.c{fill:none;stroke:#222222;}.j{fill:#000000;font-family:Verdana,Sans-serif;font-size:12px;}.l{fill:#90d9ff;stroke:#222222;}.r{fill:#d3f0ff;stroke:#222222;}</style></defs><path class="c" d="M0 22h25m53 0h10m101 0h10m30 0h10m28 0h10m132 0h10m28 0h20m-457 0q5 0 5 5v8q0 5 5 5h432q5 0 5-5v-8q0-5 5-5m5 0h30m46 0h10m133 0h10m30 0h30m45 0h22m-82 0q5 0 5 5v20q0 5 5 5h5m47 0h5q5 0 5-5v-20q0-5 5-5m5 0h20m-361 0q5 0 5 5v38q0 5 5 5h336q5 0 5-5v-38q0-5 5-5m5 0h5"/><rect class="l" x="25" y="5" width="53" height="25" rx="7"/><text class="j" x="35" y="22">WITH</text><rect class="l" x="88" y="5" width="101" height="25" rx="7"/><text class="j" x="98" y="22">REPLICATION</text><rect class="l" x="199" y="5" width="30" height="25" rx="7"/><text class="j" x="209" y="22">=</text><rect class="l" x="239" y="5" width="28" height="25" rx="7"/><text class="j" x="249" y="22">{</text><a xlink:href="#keyspace_property"><rect class="r" x="277" y="5" width="132" height="25"/><text class="j" x="287" y="22">keyspace_property</text></a><rect class="l" x="419" y="5" width="28" height="25" rx="7"/><text class="j" x="429" y="22">}</text><rect class="l" x="497" y="5" width="46" height="25" rx="7"/><text class="j" x="507" y="22">AND</text><rect class="l" x="553" y="5" width="133" height="25" rx="7"/><text class="j" x="563" y="22">DURABLE_WRITES</text><rect class="l" x="696" y="5" width="30" height="25" rx="7"/><text class="j" x="706" y="22">=</text><rect class="l" x="756" y="5" width="45" height="25" rx="7"/><text class="j" x="766" y="22">true</text><rect class="l" x="756" y="35" width="47" height="25" rx="7"/><text class="j" x="766" y="52">false</text></svg>

### Grammar
```
create_keyspace ::= CREATE { KEYSPACE | SCHEMA } [ IF NOT EXISTS ] keyspace_name
                       [ WITH REPLICATION '=' '{' keyspace_property '}']
                       [ AND DURABLE_WRITES '=' { true | false } ]

keyspace_property ::= property_name = property_value
```
Where

- `keyspace_name` and `property_name` are identifiers.
- `property_value` is a literal of either [boolean](../type_bool), [text](../type_text), or [map](../type_collection) datatype.

## Semantics

- An error is raised if the specified `keyspace_name` already exists unless `IF NOT EXISTS` option is present.
- CQL keyspace properties are supported in the syntax but have no effect internally (where YugaByte defaults are used instead).

## Examples
``` sql
cqlsh> CREATE KEYSPACE example;

cqlsh> DESCRIBE KEYSPACES;
example  system_schema  system  default_keyspace

cqlsh> DESCRIBE example;
CREATE KEYSPACE example WITH REPLICATION = {'class': 'SimpleStrategy', 'replication_factor': '3'} AND DURABLE_WRITES = true;

cqlsh> CREATE SCHEMA example;
SQL error: Keyspace Already Exists
CREATE SCHEMA example;
^^^^^^
```

## See Also
[`DROP KEYSPACE`](../ddl_drop_keyspace)
[`USE`](../ddl_use)
[Other CQL Statements](..)
