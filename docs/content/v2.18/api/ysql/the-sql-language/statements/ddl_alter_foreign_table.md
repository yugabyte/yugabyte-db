---
title: ALTER FOREIGN TABLE statement [YSQL]
headerTitle: ALTER FOREIGN TABLE
linkTitle: ALTER FOREIGN TABLE
description: Use the ALTER FOREIGN TABLE statement to alter a foreign table.
menu:
  v2.18:
    identifier: ddl_alter_foreign_table
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER FOREIGN TABLE` command to alter a foreign table.

## Syntax

{{%ebnf%}}
  alter_foreign_table
{{%/ebnf%}}

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
