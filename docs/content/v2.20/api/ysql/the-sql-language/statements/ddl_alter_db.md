---
title: ALTER DATABASE statement [YSQL]
headerTitle: ALTER DATABASE
linkTitle: ALTER DATABASE
description: Use the ALTER DATABASE statement to redefine the attributes of a database.
menu:
  v2.20:
    identifier: ddl_alter_db
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER DATABASE` statement to redefine the attributes of a database.

## Syntax

{{%ebnf%}}
  alter_database,
  alter_database_option
{{%/ebnf%}}

## Semantics

{{< note title="Note" >}}

Some options in DATABASE are under development.

{{< /note >}}

### *name*

Specify the name of the database to be altered.

### ALLOW_CONNECTIONS

Specify `false` to disallow connections to this database. Default is `true`, which allows this database to be cloned by any user with `CREATEDB` privileges.

### CONNECTION_LIMIT

Specify how many concurrent connections can be made to this database. Default of `-1` allows unlimited concurrent connections.

### IS_TEMPLATE

S`true` — This database can be cloned by any user with `CREATEDB` privileges.
Specify `false` to Only superusers or the owner of the database can clone it.

## See also

- [`CREATE DATABASE`](../ddl_create_database)
- [`DROP DATABASE`](../ddl_drop_database)
- [`SET`](../cmd_set)
