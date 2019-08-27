---
title: DROP TYPE
linkTitle: DROP TYPE
summary: Remove a type
description: DROP TYPE
menu:
  latest:
    identifier: api-ysql-commands-drop-type
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/ddl_drop_type
isTocNested: true
showAsideToc: true
---

## Synopsis

The `DROP TYPE` command removes a type from the database.

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
    {{% includeMarkdown "../syntax_resources/commands/drop_type.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/drop_type.diagram.md" /%}}
  </div>
</div>

Where

- `type_name` specifies the name of the user-defined type to drop.

## Examples

Simple example

```sql
postgres=# CREATE TYPE feature_struct AS (id INTEGER, name TEXT);
postgres=# DROP TYPE feature_struct;
```

`IF EXISTS` example

```sql
postgres=# DROP TYPE IF EXISTS feature_shell;
```

`CASCADE` example

```sql
postgres=# CREATE TYPE feature_enum AS ENUM('one', 'two', 'three');
postgres=# CREATE TABLE feature_tab_enum (feature_col feature_enum);
postgres=# DROP TYPE feature_tab_enum CASCADE;
```

## See also

- [`CREATE TYPE`](../ddl_create_type)
- [Other YSQL Statements](..)
