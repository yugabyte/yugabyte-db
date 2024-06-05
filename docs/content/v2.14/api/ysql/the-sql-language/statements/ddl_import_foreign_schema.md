---
title: IMPORT FOREIGN SCHEMA statement [YSQL]
headerTitle: IMPORT FOREIGN SCHEMA
linkTitle: IMPORT FOREIGN SCHEMA
description: Use the IMPORT FOREIGN SCHEMA statement to import tables from a foreign schema.
menu:
  v2.14:
    identifier: ddl_import_foreign_schema
    parent: statements
type: docs
---

## Synopsis

Use the `IMPORT FOREIGN SCHEMA`  command to create foreign tables that represent tables in a foreign schema on a foreign server. The newly created foreign tables are owned by the user who issued the command.
By default, all the tables in the foreign schema are imported. However, the user can specify which tables to import using the `LIMIT TO` and `EXCEPT` clauses.
To use this command, the user must have `USAGE` privileges on the foreign server, and `CREATE` privileges on the target schema.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/import_foreign_schema.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/import_foreign_schema.diagram.md" %}}
  </div>
</div>

## Semantics

Create foreign tables (in *local_schema*) that represent tables in a foreign schema named *remote_schema* located on a foreign server named *server_name*.

### LIMIT TO
The `LIMIT TO` clause can be optionally used to specify which tables to import. All other tables will be ignored.

### EXCEPT
The `EXCEPT` clause can be optionally used to specify which tables to *not* import. All other tables will be ignored.


## See also

- [`CREATE SERVER`](../ddl_create_server)
- [`CREATE FOREIGN TABLE`](../ddl_create_foreign_table)
- [`IMPORT FOREIGN SCHEMA`](../ddl_import_foreign_schema)
