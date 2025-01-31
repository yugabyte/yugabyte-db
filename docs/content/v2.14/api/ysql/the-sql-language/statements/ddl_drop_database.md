---
title: DROP DATABASE statement [YSQL]
headerTitle: DROP DATABASE
linkTitle: DROP DATABASE
description: Use the DROP DATABASE statement to remove a database and all of its associated objects from the system.
menu:
  v2.14:
    identifier: ddl_drop_database
    parent: statements
type: docs
---

## Synopsis

Use the `DROP DATABASE` statement to remove a database and all of its associated objects from the system. This is an irreversible statement. A currently-open connection to the database will be invalidated and then closed as soon as the statement is executed using that connection.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-bs-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-bs-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_database.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/drop_database.diagram.md" %}}
  </div>
</div>

## Semantics

### *drop_database*

#### DROP DATABASE [ IF EXISTS ] *database_name*

Remove a database and all associated objects. All objects that are associated with `database_name` such as tables will be invalidated after the drop statement is completed. All connections to the dropped database would be invalidated and eventually disconnected.

### *database_name*

Specify the name of the database.

## See also

- [`CREATE DATABASE`](../ddl_create_database)
