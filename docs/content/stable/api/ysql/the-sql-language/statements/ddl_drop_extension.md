---
title: DROP EXTENSION statement [YSQL]
headerTitle: DROP EXTENSION
linkTitle: DROP EXTENSION
summary: Remove an extension
description: Use the DROP EXTENSION statement to remove an extension from the database
menu:
  stable:
    identifier: ddl_drop_extension
    parent: statements
type: docs
---

## Synopsis

Use the `DROP EXTENSION` statement to remove an extension from the database.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link active" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_extension.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade show active" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_extension.diagram.md" %}}
  </div>
</div>

## Semantics

- An error is thrown if the extension does not exist unless `IF EXISTS` is
  used. Then, a notice is issued instead.
- `RESTRICT` is the default, and it will not drop the extension if any objects
  depend on it.
- `CASCADE` drops any objects that transitively depend on the extension.

## Examples

```plpgsql
DROP EXTENSION IF EXISTS cube;
```

```output
NOTICE:  extension "cube" does not exist, skipping
```

```plpgsql
CREATE EXTENSION cube;
CREATE EXTENSION earthdistance;
DROP EXTENSION IF EXISTS cube RESTRICT;
```

```output
ERROR:  cannot drop extension cube because other objects depend on it
DETAIL:  extension earthdistance depends on function cube_out(cube)
HINT:  Use DROP ... CASCADE to drop the dependent objects too.
```

```plpgsql
DROP EXTENSION IF EXISTS cube CASCADE;
```

```output
NOTICE:  drop cascades to extension earthdistance
DROP EXTENSION
```

## See also

- [PostgreSQL Extensions](../../../../../explore/ysql-language-features/pg-extensions/)
- [CREATE EXTENSION](../ddl_create_extension)
