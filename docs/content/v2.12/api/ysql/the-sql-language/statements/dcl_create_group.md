---
title: CREATE GROUP statement [YSQL]
headerTitle: CREATE GROUP
linkTitle: CREATE GROUP
description: Use the CREATE GROUP statement to create a group role. CREATE GROUP is an alias for CREATE ROLE and is used to create a group role.
menu:
  v2.12:
    identifier: dcl_create_group
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE GROUP` statement to create a group role. `CREATE GROUP` is an alias for [`CREATE ROLE`](../dcl_create_role) and is used to create a group role.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_group,role_option.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_group,role_option.diagram.md" %}}
  </div>
</div>


See [`CREATE ROLE`](../dcl_create_role) for more details.

## Examples

- Create a sample group that can manage databases and roles.

```plpgsql
yugabyte=# CREATE GROUP SysAdmin WITH CREATEDB CREATEROLE;
```

## See also

- [`CREATE ROLE`](../dcl_create_role)
- [`GRANT`](../dcl_grant)
- [`REVOKE`](../dcl_revoke)
