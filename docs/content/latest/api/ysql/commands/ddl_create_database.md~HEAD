---
title: CREATE DATABASE
summary: Create a new database
description: CREATE DATABASE
menu:
  latest:
    identifier: api-ysql-commands-create-db
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/ddl_create_database
isTocNested: true
showAsideToc: true
---

## Synopsis
The `CREATE DATABASE` statement creates a `database` that functions as a grouping mechanism for database objects such as [tables](../ddl_create_table).

## Syntax

### Diagram

### Grammar
```
create_database ::=
  CREATE DATABASE name
  [ [ WITH ] [ OWNER [=] user_name ]
    [ TEMPLATE [=] template ]
    [ ENCODING [=] encoding ]
    [ LC_COLLATE [=] lc_collate ]
    [ LC_CTYPE [=] lc_ctype ]
    [ TABLESPACE [=] tablespace_name ]
    [ ALLOW_CONNECTIONS [=] allowconn ]
    [ CONNECTION LIMIT [=] connlimit ]
    [ IS_TEMPLATE [=] istemplate ] ]
```
Where

- `name` is an identifier that specifies the database to be created.

- `user_name` specifies the user who will own the new database. When not specified, the database creator is the owner.

- `template` specifies name of the template from which the new database is created.

- `encoding` specifies the character set encoding to use in the new database.

- lc_collate specifies the collation order (LC_COLLATE).

- lc_ctype specifies the character classification (LC_CTYPE).

- tablespace_name specifies the tablespace that is associated with the database to be created.

- allowconn is either `true` or `false`.

- connlimit specifies the number of concurrent connections can be made to this database. -1 means there is no limit.

- istemplate is either `true` or `false`.

## Semantics

- An error is raised if a database with the specified `name` already exists.

- Some options in DATABASE are under development.

## See Also
[`ALTER DATABASE`](../ddl_alter_db)
[Other PostgreSQL Statements](..)
