---
title: CREATE FOREIGN TABLE statement [YSQL]
headerTitle: CREATE FOREIGN TABLE
linkTitle: CREATE FOREIGN TABLE
description: Use the CREATE FOREIGN TABLE statement to create a foreign table.
menu:
  v2.14:
    identifier: ddl_create_foreign_table
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE FOREIGN TABLE` command to create a foreign table.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_foreign_table.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_foreign_table.diagram.md" %}}
  </div>
</div>

## Semantics

Create a new foreign table named *table_name*. If *table_name* already exists in the specified database, an error will be raised unless the `IF NOT EXISTS` clause is used.

### Collation
The `COLLATE` clause can be used to specify a collation for the column.

### Server

The `SERVER` clause can be used to specify the name of the foreign server to use.

### Options:
The `OPTIONS` clause specifies options for the foreign table. The permitted option names and values are specific to each foreign data wrapper. The options are validated using the FDW’s validator function.

## Examples

Basic example.

```plpgsql
yugabyte=#  CREATE FOREIGN TABLE mytable (col1 int, col2 int) SERVER my_server OPTIONS (schema 'external_schema', table 'external_table');
```

## See also

- [`CREATE FOREIGN DATA WRAPPER`](../ddl_create_foreign_data_wrapper/)
- [`CREATE FOREIGN TABLE`](../ddl_create_foreign_table/)
- [`CREATE SERVER`](../ddl_create_server/)
- [`CREATE USER MAPPING`](../ddl_create_user_mapping/)
- [`IMPORT FOREIGN SCHEMA`](../ddl_import_foreign_schema/)
- [`ALTER FOREIGN TABLE`](../ddl_alter_foreign_table/)
- [`DROP FOREIGN TABLE`](../ddl_drop_foreign_table/)
