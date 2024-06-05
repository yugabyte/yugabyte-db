---
title: DROP POLICY statement [YSQL]
headerTitle: DROP POLICY
linkTitle: DROP POLICY
description: Use the DROP POLICY statement to remove the specified row level security policy from the table.
menu:
  v2.14:
    identifier: dcl_drop_policy
    parent: statements
type: docs
---

## Synopsis

Use the `DROP POLICY` statement to remove the specified row level security policy from the table. Note that if all
policies for a table are removed and the table still has `ENABLE ROW LEVEL SECURITY`, then a default
deny all policy will be applied for the table.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_policy.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_policy.diagram.md" %}}
  </div>
</div>

Where

- `name` is the name of the policy to be removed.
- `table_name` is the name of the table that the policy is on.
- `CASCADE` / `RESTRICT` don't have any effect because table policies don't have any dependent objects.

## Example

- Drop a policy.

```plpgsql
yugabyte=# DROP POLICY p1 ON table_foo;
```

## See also

- [`ALTER POLICY`](../dcl_alter_policy)
- [`CREATE POLICY`](../dcl_create_policy)
- [`ALTER TABLE`](../ddl_alter_table)
