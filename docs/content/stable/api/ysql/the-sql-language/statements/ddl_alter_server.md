---
title: ALTER SERVER statement [YSQL]
headerTitle: ALTER SERVER
linkTitle: ALTER SERVER
description: Use the ALTER SERVER statement to create alter a foreign server.
menu:
  stable:
    identifier: ddl_alter_server
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER SERVER` command to alter the definition of a foreign server. The command can be used to alter the options of the foreign server, or to change its owner.

## Syntax

{{%ebnf%}}
  alter_server
{{%/ebnf%}}

## Semantics

Alter the foreign server named **server_name**.

### Version
The `VERSION` clause can be used to specify the updated version of the server.

### Options
The `OPTIONS` clause can be used to specify the new options of the foreign server. `ADD`, `SET`, and `DROP` specify the action to be performed. `ADD` is assumed if no operation is explicitly specified.

## Examples

Change the server's version to `2.0`, set `opt1` to `'true'`, and drop `opt2`.
```plpgsql
yugabyte=# ALTER SERVER my_server SERVER VERSION '2.0' OPTIONS (SET opt1 'true', DROP opt2);
```
## See also

- [`CREATE SERVER`](../ddl_create_server/)
- [`DROP SERVER`](../ddl_drop_server/)
