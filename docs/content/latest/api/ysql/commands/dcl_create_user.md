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

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <i class="fas fa-file-alt" aria-hidden="true"></i>
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <i class="fas fa-project-diagram" aria-hidden="true"></i>
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
    {{% includeMarkdown "../syntax_resources/commands/create_user.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/create_user.diagram.md" /%}}
  </div>
</div>


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

## See also

[Other YSQL Statements](..)