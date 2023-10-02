---
title: DROP FOREIGN TABLE statement [YSQL]
headerTitle: DROP FOREIGN TABLE
linkTitle: DROP FOREIGN TABLE
description: Use the DROP FOREIGN TABLE statement to drop a foreign table.
menu:
  v2.16:
    identifier: ddl_drop_foreign_table
    parent: statements
type: docs
---

## Synopsis

Use the `DROP FOREIGN TABLE` command to remove a foreign table. The user who executes the command must be the owner of the foreign table.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_foreign_table.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_foreign_table.diagram.md" %}}
  </div>
</div>

## Semantics

Drop a foreign table named **table_name**. If it doesn’t exist in the database, an error will be thrown unless the `IF EXISTS` clause is used.

### RESTRICT/CASCADE:
`RESTRICT` is the default and it will not drop the foreign table if any objects depend on it.
`CASCADE` will drop the foreign table and any objects that transitively depend on it.

## Examples

Drop the foreign-data table `mytable`, along with any objects that depend on it.

```plpgsql
yugabyte=# DROP FOREIGN TABLE mytable CASCADE;
```
## See also

- [`CREATE FOREIGN TABLE`](../ddl_create_foreign_table/)
- [`ALTER FOREIGN TABLE`](../ddl_alter_foreign_table/)
