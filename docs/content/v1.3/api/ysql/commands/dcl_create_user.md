---
title: CREATE USER
linkTitle: CREATE USER
description: Users and roles
summary: CREATE USER
block_indexing: true
menu:
  v1.3:
    identifier: api-ysql-commands-create-users
    parent: api-ysql-commands
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `CREATE USER` statement, and limited `GRANT` or `REVOKE` statements, to create new roles and set or remove permissions.

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

## Semantics

### *name*

Specify the new database role.

## Examples

### Create a sample role

```sql
postgres=# CREATE USER John;
CREATE ROLE
```

Note that `CREATE ROLE` is returned because `CREATE USER` is an alias for `CREATE ROLE`.
