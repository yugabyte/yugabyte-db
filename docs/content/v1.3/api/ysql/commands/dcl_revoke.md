---
title: REVOKE
linkTitle: REVOKE
description: REVOKE
summary: REVOKE
block_indexing: true
menu:
  v1.3:
    identifier: api-ysql-commands-revoke
    parent: api-ysql-commands
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `REVOKE` statement to remove access privileges.

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
    {{% includeMarkdown "../syntax_resources/commands/revoke.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/revoke.diagram.md" /%}}
  </div>
</div>

## Semantics

Not all `REVOKE` options are supported yet in YSQL, but the following `REVOKE` option is supported.

```
REVOKE ALL ON DATABASE name FROM name;
```

For the list of possible *privileges* or *privilege_target* settings, see `GRANT` in the PostgreSQL documentation.

### REVOKE

Remove privileges on a database object or a membership in a role.

### *privileges*

Specify the privileges.

#### ALL

Specify all of the available privileges.

### *privilege_target*

### *name*

Specify the name of the role.

### CASCADE

### RESTRICT

## Examples

- Remove John's privileges from the `postgres` database.

```sql
postgres=# REVOKE ALL ON DATABASE postgres FROM John;
```

## See also

- [`CREATE USER`](../dcl_create_user)
- [`REVOKE`](../dcl_revoke)
