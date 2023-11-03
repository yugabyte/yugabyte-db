---
title: DROP OWNED statement [YSQL]
headerTitle: DROP OWNED
linkTitle: DROP OWNED
description: Use the DROP OWNED statement to drop all database objects within the current database that are owned by one of the specified roles.
menu:
  v2.12:
    identifier: dcl_drop_owned
    parent: statements
type: docs
---

## Synopsis

Use the `DROP OWNED` statement to drop all database objects within the current database that are owned by one of the specified roles.
Any privileges granted to the given roles on objects in the current database or on shared objects will also be revoked.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_owned,role_specification.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_owned,role_specification.diagram.md" %}}
  </div>
</div>

## Semantics

- CASCADE

Automatically drop objects that depend on the affected objects.

- RESTRICT

This is the default mode and will raise an error if there are other database objects that depend on the dropped objects.

## Examples

- Drop all objects owned by `john`.

```plpgsql
yugabyte=# drop owned by john;
```

## See also

- [`REASSIGN OWNED`](../dcl_reassign_owned)
- [`CREATE ROLE`](../dcl_create_role)
- [`GRANT`](../dcl_grant)
- [`REVOKE`](../dcl_revoke)
