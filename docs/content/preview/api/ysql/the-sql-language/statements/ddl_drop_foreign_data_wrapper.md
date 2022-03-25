---
title: DROP FOREIGN DATA WRAPPER statement [YSQL]
headerTitle: DROP FOREIGN DATA WRAPPER
linkTitle: DROP FOREIGN DATA WRAPPER
description: Use the DROP FOREIGN DATA WRAPPER statement to drop a foreign-data wrapper.
menu:
  latest:
    identifier: ddl_drop_foreign_data_wrapper
    parent: statements
aliases:
  - /latest/api/ysql/commands/ddl_drop_foreign_data_wrapper/
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `DROP FOREIGN DATA WRAPPER` command to remove a foreign-data wrapper. The user who executes the command must be the owner of the foreign-data wrapper.

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
    {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_foreign_data_wrapper.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_foreign_data_wrapper.diagram.md" /%}}
  </div>
</div>

## Semantics

Drop a foreign-data wrapper named **fdw_name**. If it doesn’t exist in the database, an error will be thrown unless the `IF EXISTS` clause is used.

### RESTRICT/CASCADE:
`RESTRICT` is the default and it will not drop the foreign-data wrapper if any objects depend on it. 
`CASCADE` will drop the foreign-data wrapper and any objects that transitively depend on it.

## See also

- [`CREATE SERVER`](../ddl_create_server)
- [`CREATE FOREIGN TABLE`](../ddl_create_foreign_table)
- [`IMPORT FOREIGN SCHEMA`](../ddl_import_foreign_schema)
