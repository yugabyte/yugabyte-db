---
title: CREATE USER
description: Users and roles
summary: Users and roles
menu:
  latest:
    identifier: api-ysql-commands-create-users
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/dcl_create_user
isTocNested: true
showAsideToc: true
---

## Synopsis 

YugaByte supports the `CREATE USER` and limited `GRANT`/`REVOKE` commands to create new roles and set/remove permissions.

## Syntax

### Diagrams

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="205" height="34" viewbox="0 0 205 34"><path class="connector" d="M0 21h5m67 0h10m53 0h10m55 0h5"/><rect class="literal" x="5" y="5" width="67" height="24" rx="7"/><text class="text" x="15" y="21">CREATE</text><rect class="literal" x="82" y="5" width="53" height="24" rx="7"/><text class="text" x="92" y="21">USER</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="145" y="5" width="55" height="24"/><text class="text" x="155" y="21">name</text></a></svg>

### Grammar

```
create_user ::= CREATE USER name ;
```

- Not all GRANT and REVOKE options are supported yet in YSQL, but the following GRANT and REVOKE statements are supported in YSQL.
```
postgres=# GRANT ALL ON DATABASE name TO name;
postgres=# REVOKE ALL ON DATABASE name FROM name;
```

- For the list of possible `privileges` or `privilege_target`s see [this](https://www.postgresql.org/docs/9.0/static/sql-grant.html) page.

## Examples

- Create a sample role.

```sql
postgres=# CREATE USER John;
```

- Grant John all permissions on the `postgres` database.

```sql
postgres=# GRANT ALL ON DATABASE postgres TO John;
```

- Remove John's permissions from the `postgres` database.

```sql
postgres=# REVOKE ALL ON DATABASE postgres FROM John;
```

## See Also

[Other YSQL Statements](..)