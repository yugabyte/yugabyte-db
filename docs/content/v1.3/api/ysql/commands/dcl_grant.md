---
title: GRANT
linkTitle: GRANT
description: GRANT
summary: GRANT statement
block_indexing: true
menu:
  v1.3:
    identifier: api-ysql-commands-grant
    parent: api-ysql-commands
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `GRANT` statement to allow access privileges.

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
    {{% includeMarkdown "../syntax_resources/commands/grant.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/grant.diagram.md" /%}}
  </div>
</div>

## Semantics

Not all `GRANT` options are supported yet in YSQL, but the following `GRANT` option is supported.

```
GRANT ALL ON DATABASE name TO name;
```

For the list of possible privileges or privilege_target settings, see REVOKE in the PostgreSQL documentation.

### GRANT

Grant privileges on a database object or grant a membership in a role.

### *privileges*

Specify the privileges.

#### ALL

Grant all of the available privileges.

### *privilege_target*

### *name*

Specify the name of the role.

### WITH GRANT OPTION

Specify that the recipient of the privileges can in turn grant them to others. Grant options cannot be granted to `PUBLIC`.

## Examples

- Grant John all privileges on the `postgres` database.

```sql
postgres=# GRANT ALL ON DATABASE postgres TO John;
```

## See also

- [REVOKE](../dcl_revoke)
