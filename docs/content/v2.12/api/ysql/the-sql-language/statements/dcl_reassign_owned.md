---
title: REASSIGN OWNED statement [YSQL]
headerTitle: REASSIGN OWNED
linkTitle: REASSIGN OWNED
description: Use the REASSIGN OWNED statement to change the ownership of database objects owned by any of the "old_roles" to "new_role".
menu:
  v2.12:
    identifier: dcl_reassign_owned
    parent: statements
type: docs
---

## Synopsis

Use the `REASSIGN OWNED` statement to change the ownership of database objects owned by any of the `old_roles` to `new_role`.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/reassign_owned,role_specification.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/reassign_owned,role_specification.diagram.md" %}}
  </div>
</div>

## Semantics

`REASSIGN OWNED` is typically used to prepare for the removal of a role. It requires membership on both the source roles and target role.

## Examples

- Reassign all objects owned by john to yugabyte.

```plpgsql
yugabyte=# reassign owned by john to yugabyte;
```

## See also

- [`DROP OWNED`](../dcl_drop_owned)
- [`CREATE ROLE`](../dcl_create_role)
- [`GRANT`](../dcl_grant)
- [`REVOKE`](../dcl_revoke)
