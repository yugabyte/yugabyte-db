---
title: DROP TABLE
summary: Remove a table
---

## Synopsis
The `DROP TABLE` statement removes a table and all of its data from the database.

## Syntax

### Diagram
<svg version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="388" height="50" viewbox="0 0 388 50"><defs><style type="text/css">.c{fill:none;stroke:#222222;}.j{fill:#000000;font-family:Verdana,Sans-serif;font-size:12px;}.l{fill:#90d9ff;stroke:#222222;}.r{fill:#d3f0ff;stroke:#222222;}</style></defs><path class="c" d="M0 22h5m53 0h10m58 0h30m32 0h10m64 0h20m-141 0q5 0 5 5v8q0 5 5 5h116q5 0 5-5v-8q0-5 5-5m5 0h10m91 0h5"/><rect class="l" x="5" y="5" width="53" height="25" rx="7"/><text class="j" x="15" y="22">DROP</text><rect class="l" x="68" y="5" width="58" height="25" rx="7"/><text class="j" x="78" y="22">TABLE</text><rect class="l" x="156" y="5" width="32" height="25" rx="7"/><text class="j" x="166" y="22">IF</text><rect class="l" x="198" y="5" width="64" height="25" rx="7"/><text class="j" x="208" y="22">EXISTS</text><a xlink:href="#table_name"><rect class="r" x="292" y="5" width="91" height="25"/><text class="j" x="302" y="22">table_name</text></a></svg>

### Grammar
```
drop_table ::= DROP TABLE [ IF EXISTS ] table_name;
```
Where

- `table_name` is an identifier (possibly qualified with a keyspace name).

## Semantics

 - An error is raised if the specified `table_name` does not exist unless `IF EXISTS` option is present.
 - Associated objects to `table_name` such as prepared statements will be eventually invalidated after the drop statement is completed.

## Examples

```
cqlsh:example> CREATE TABLE users(id INT PRIMARY KEY, name TEXT);
cqlsh:example> DROP TABLE users;
```

## See Also

[`ALTER TABLE`](../ddl_alter_table)
[`CREATE TABLE`](../ddl_create_table)
[`DELETE`](../dml_delete)
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[Other CQL Statements](..)
