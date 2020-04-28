---
title: DROP EXTENSION statement [YSQL]
headerTitle: DROP EXTENSION
linkTitle: DROP EXTENSION
summary: Remove an extension
description: Use the DROP EXTENSION statement to remove an extension from the database
menu:
  latest:
    identifier: api-ysql-commands-drop-extension
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/ddl_drop_extension/
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `DROP EXTENSION` statement to remove an extension from the database.

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
    {{% includeMarkdown "../syntax_resources/commands/drop_extension.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/drop_extension.diagram.md" /%}}
  </div>
</div>

## Semantics

- An error will be thrown if the extension does not exist unless `IF EXISTS` is
  used.  Then, a notice is issued instead.
- `RESTRICT` is the default, and it will not drop the extension if any objects
  depend on it.
- `CASCADE` will drop any objects that transitively depend on the extension.

## Examples

```postgresql
DROP EXTENSION IF EXISTS cube;
CREATE EXTENSION cube;
CREATE EXTENSION earthdistance;
DROP EXTENSION IF EXISTS cube RESTRICT;
DROP EXTENSION IF EXISTS cube CASCADE;
```

## See also

- [Extensions page](../../extensions)
