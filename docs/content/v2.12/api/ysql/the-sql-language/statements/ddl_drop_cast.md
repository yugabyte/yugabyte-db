---
title: DROP CAST statement [YSQL]
headerTitle: DROP CAST
linkTitle: DROP CAST
description: Use the DROP CAST statement to remove a cast.
menu:
  v2.12:
    identifier: ddl_drop_cast
    parent: statements
type: docs
---

## Synopsis

Use the `DROP CAST` statement to remove a cast.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_cast.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_cast.diagram.md" %}}
  </div>
</div>

## Semantics

See the semantics of each option in the [PostgreSQL docs][postgresql-docs-drop-cast].

## Examples

Basic example.

```plpgsql
yugabyte=# CREATE FUNCTION sql_to_date(integer) RETURNS date AS $$
             SELECT $1::text::date
             $$ LANGUAGE SQL IMMUTABLE STRICT;
yugabyte=# CREATE CAST (integer AS date) WITH FUNCTION sql_to_date(integer) AS ASSIGNMENT;
yugabyte=# DROP CAST (integer AS date);
```

## See also

- [`CREATE CAST`](../ddl_create_cast)
- [postgresql-docs-drop-cast](https://www.postgresql.org/docs/current/sql-dropcast.html)
