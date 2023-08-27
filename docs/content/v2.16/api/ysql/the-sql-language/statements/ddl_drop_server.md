---
title: DROP SERVER statement [YSQL]
headerTitle: DROP SERVER
linkTitle: DROP SERVER
description: Use the DROP SERVER statement to drop a foreign server.
menu:
  v2.16:
    identifier: ddl_drop_foreign_server
    parent: statements
type: docs
---

## Synopsis

Use the `DROP SERVER` command to remove a foreign server. The user who executes the command must be the owner of the foreign server.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_server.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_server.diagram.md" %}}
  </div>
</div>

## Semantics

Drop a foreign server named **server_name**. If it doesn’t exist in the database, an error will be thrown unless the `IF EXISTS` clause is used.

### RESTRICT/CASCADE:
`RESTRICT` is the default and it will not drop the foreign server if any objects depend on it.
`CASCADE` will drop the foreign server and any objects that transitively depend on it.

## Examples

Drop the foreign-data wrapper `my_server`, along with any objects that depend on it.

```plpgsql
yugabyte=# DROP SERVER my_server CASCADE;
```
## See also

- [`CREATE SERVER`](../ddl_create_server/)
- [`ALTER SERVER`](../ddl_alter_server/)
