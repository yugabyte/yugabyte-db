---
title: DROP USER statement [YSQL]
headerTitle: DROP USER
linkTitle: DROP USER
description: Use the DROP USER statement to drop a user or role. DROP USER is an alias for DROP ROLE.
menu:
  v2.14:
    identifier: dcl_drop_user
    parent: statements
type: docs
---

## Synopsis

Use the `DROP USER` statement to drop a user or role. `DROP USER` is an alias for [`DROP ROLE`](../dcl_drop_role) and is used to drop a role.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-bs-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-bs-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_user.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_user.diagram.md" %}}
  </div>
</div>

## Semantics

See [`DROP ROLE`](../dcl_drop_role) for more details.

## Example

- Drop a user.

```plpgsql
yugabyte=# DROP USER John;
```

## See also

- [`CREATE ROLE`](../dcl_create_role)
- [`ALTER ROLE`](../dcl_alter_role)
- [`GRANT`](../dcl_grant)
- [`REVOKE`](../dcl_revoke)
