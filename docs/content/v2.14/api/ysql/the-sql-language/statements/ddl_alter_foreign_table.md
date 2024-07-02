---
title: ALTER FOREIGN TABLE statement [YSQL]
headerTitle: ALTER FOREIGN TABLE
linkTitle: ALTER FOREIGN TABLE
description: Use the ALTER FOREIGN TABLE statement to alter a foreign table.
menu:
  v2.14:
    identifier: ddl_alter_foreign_table
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER FOREIGN TABLE` command to alter a foreign table.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/alter_foreign_table.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/alter_foreign_table.diagram.md" %}}
  </div>
</div>

## Semantics

Alter the foreign table named *table_name*.

### Add a column
The `ADD COLUMN` clause can be used to add a new column to the foreign table. There's no effect on the underlying storage: the `ADD COLUMN` action just indicates that the newly added column can be accessed through the foreign table.

### Drop a column

The `DROP COLUMN` clause can be used to drop a column from the foreign table. `CASCADE` or `RESTRICT` can be specified.

### Change owner
The `OWNER TO` clause can be used to specify the new_owner.

### Options
The `OPTIONS` clause can be used to specify the new options of the foreign table. `ADD`, `SET`, and `DROP` specify the action to be performed. `ADD` is assumed if no operation is explicitly specified.

### Rename
The `RENAME TO` clause can be used to rename the foreign table to **table_name**.

## Examples

Adding a new column.

```plpgsql
yugabyte=# ALTER FOREIGN TABLE my_table ADD COLUMN new_col int;
```

Change the options.

```plpgsql
yugabyte=# ALTER FOREIGN TABLE my_table OPTIONS (ADD newopt1 'value1', DROP oldopt1 'value2', SET oldopt2 'value3');
```

## See also

- [`CREATE FOREIGN TABLE`](../ddl_create_foreign_table/)
- [`DROP FOREIGN TABLE`](../ddl_drop_foreign_table/)
