---
title: DROP TRIGGER statement [YSQL]
headerTitle: DROP TRIGGER
linkTitle: DROP TRIGGER
description: Use the DROP TRIGGER statement to remove a trigger from the database.
menu:
  v2.14:
    identifier: ddl_drop_trigger
    parent: statements
type: docs
---

## Synopsis

Use the `DROP TRIGGER` statement to remove a trigger from the database.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_trigger.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_trigger.diagram.md" %}}
  </div>
</div>

## Semantics

- `RESTRICT` is the default and it will throw an error if any objects depend on the trigger.
- `CASCADE` will drop all objects that (transitively) depend on the trigger.


## Examples

```plpgsql
DROP TRIGGER update_moddatetime ON posts;
```

## See also

- [`CREATE TRIGGER`](../ddl_create_trigger)
- [`INSERT`](../dml_insert)
- [`UPDATE`](../dml_update/)
- [`DELETE`](../dml_delete/)
