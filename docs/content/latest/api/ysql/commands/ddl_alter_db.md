---
title: ALTER DATABASE
linkTitle: ALTER DATABASE
summary: Alter database
description: ALTER DATABASE
menu:
  latest:
    identifier: api-ysql-commands-alter-db
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/ddl_alter_db
isTocNested: true
showAsideToc: true
---

## Synopsis
ALTER DATABASE redefines the attributes of the specified database.

## Syntax

### Diagram 

### Grammar
```
alter_database ::=
  ALTER DATABASE name { [ [ WITH ] alter_database_option [ ... ] ] |
                        RENAME TO new_name |
                        OWNER TO { new_owner | CURRENT_USER | SESSION_USER } |
                        SET TABLESPACE new_tablespace |
                        SET configuration_parameter { TO | = } { value | DEFAULT } |
                        SET configuration_parameter FROM CURRENT |
                        RESET configuration_parameter |
                        RESET ALL }

alter_database_option ::=
  { ALLOW_CONNECTIONS allowconn | CONNECTION LIMIT connlimit | IS_TEMPLATE istemplate }
```

where

- `name` is an identifier that specifies the database to be altered.

- tablespace_name specifies the new tablespace that is associated with the database.

- allowconn is either `true` or `false`.

- connlimit specifies the number of concurrent connections can be made to this database. -1 means there is no limit.

- istemplate is either `true` or `false`.

## Semantics

- Some options in DATABASE are under development.

## See Also
[`CREATE DATABASE`](../ddl_create_database)
[Other PostgreSQL Statements](..)
