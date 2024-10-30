---
title: DROP FOREIGN TABLE statement [YSQL]
headerTitle: DROP FOREIGN TABLE
linkTitle: DROP FOREIGN TABLE
description: Use the DROP FOREIGN TABLE statement to drop a foreign table.
menu:
  v2.20:
    identifier: ddl_drop_foreign_table
    parent: statements
type: docs
---

## Synopsis

Use the `DROP FOREIGN TABLE` command to remove a foreign table. The user who executes the command must be the owner of the foreign table.

## Syntax

{{%ebnf%}}
  drop_foreign_table
{{%/ebnf%}}

## Semantics

Drop a foreign table named **table_name**. If it doesn’t exist in the database, an error will be thrown unless the `IF EXISTS` clause is used.

### RESTRICT/CASCADE:
`RESTRICT` is the default and it will not drop the foreign table if any objects depend on it.
`CASCADE` will drop the foreign table and any objects that transitively depend on it.

## Examples

Drop the foreign-data table `mytable`, along with any objects that depend on it.

```plpgsql
yugabyte=# DROP FOREIGN TABLE mytable CASCADE;
```
## See also

- [`CREATE FOREIGN TABLE`](../ddl_create_foreign_table/)
- [`ALTER FOREIGN TABLE`](../ddl_alter_foreign_table/)
