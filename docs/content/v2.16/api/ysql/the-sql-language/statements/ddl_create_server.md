---
title: CREATE SERVER statement [YSQL]
headerTitle: CREATE SERVER
linkTitle: CREATE SERVER
description: Use the CREATE SERVER statement to create a foreign server.
menu:
  v2.16:
    identifier: ddl_create_server
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE SERVER` command to create a foreign table.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_server.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_server.diagram.md" %}}
  </div>
</div>

## Semantics

Create a foreign server named *server_name*. If *server_name* already exists in the specified database, an error will be raised unless the `IF NOT EXISTS` clause is used.

### Type
The `TYPE` clause can be optionally used to specify the server type.

### Server Version

The `VERSION` clause can be optionally used to specify the server version.

### FDW name
The `FOREIGN DATA WRAPPER` clause can be used to specify the name of the foreign-data wrapper.

### Options:
The `OPTIONS` clause specifies options for the foreign server. They typically define the connection details of the server, but the actual permitted option names and values are specific to the server’s foreign data wrapper.

## Examples

Basic example.

```plpgsql
yugabyte=#  CREATE SERVER my_server FOREIGN DATA WRAPPER my_wrapper OPTIONS (host '187.51.62.1');
```

## See also

- [`CREATE FOREIGN DATA WRAPPER`](../ddl_create_foreign_data_wrapper/)
- [`CREATE FOREIGN TABLE`](../ddl_create_foreign_table/)
- [`CREATE USER MAPPING`](../ddl_create_user_mapping/)
- [`IMPORT FOREIGN SCHEMA`](../ddl_import_foreign_schema/)
- [`ALTER SERVER`](../ddl_alter_server/)
