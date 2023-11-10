---
title: DROP SERVER statement [YSQL]
headerTitle: DROP SERVER
linkTitle: DROP SERVER
description: Use the DROP SERVER statement to drop a foreign server.
menu:
  v2.18:
    identifier: ddl_drop_foreign_server
    parent: statements
type: docs
---

## Synopsis

Use the `DROP SERVER` command to remove a foreign server. The user who executes the command must be the owner of the foreign server.

## Syntax

{{%ebnf%}}
  drop_server
{{%/ebnf%}}

## Semantics

Drop a foreign server named **server_name**. If it doesn’t exist in the database, an error will be thrown unless the `IF EXISTS` clause is used.

### RESTRICT/CASCADE:
`RESTRICT` is the default and it will not drop the foreign server if any objects depend on it.
`CASCADE` will drop the foreign server and any objects that transitively depend on it.

## Examples

Drop the foreign-data wrapper `my_server`, along with any objects that depend on it.

```plpgsql
yugabyte=# DROP SERVER my_server CASCADE;
```
## See also

- [`CREATE SERVER`](../ddl_create_server/)
- [`ALTER SERVER`](../ddl_alter_server/)
