---
title: ALTER TYPE statement [YSQL]
headerTitle: ALTER TYPE
linkTitle: ALTER TYPE
description: Use the ALTER TYPE statement to change the definition of a user-defined type.
menu:
  v2025.2_api:
    identifier: ddl_alter_type
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER TYPE` statement to change the definition of a user-defined type, such as renaming the type, changing its owner or schema, adding or renaming the values of an enumerated type, or dropping an attribute of a composite type.

## Syntax

{{%ebnf%}}
  alter_type_rename,
  alter_type_owner,
  alter_type_set_schema,
  alter_type_rename_value,
  alter_type_add_value,
  alter_type_drop_attribute
{{%/ebnf%}}

## Semantics

### RENAME TO

Change the name of the type. Columns and other objects that use the type continue to work; they refer to the type by its new name.

### OWNER TO

Change the owner of the type. You must be a member of the new owning role to make this change.

### SET SCHEMA

Move the type to a different schema.

### RENAME VALUE

Rename a value of an enumerated type. An error is raised if `existing_enum_value` is not a value of the type, or if `new_enum_value` already is.

### ADD VALUE

Add a new value to an enumerated type. Use `BEFORE` or `AFTER` to place the new value at a specific position in the enum's sort ordering; by default the new value is added at the end. If `IF NOT EXISTS` is specified and the value already exists, a notice is issued instead of an error.

### DROP ATTRIBUTE

`DROP ATTRIBUTE` is supported in v2025.2.5.0+.

Drop an attribute from a composite type. If `IF EXISTS` is specified and the attribute does not exist, a notice is issued instead of an error. You can drop multiple attributes in a single statement by separating the `DROP ATTRIBUTE` actions with commas.

## Limitations

- `ALTER TYPE ... ADD ATTRIBUTE`, `ALTER TYPE ... ALTER ATTRIBUTE ... TYPE`, and `ALTER TYPE ... RENAME ATTRIBUTE` are not supported. See issue [#1893](https://github.com/yugabyte/yugabyte-db/issues/1893).
- `ALTER TYPE ... DROP ATTRIBUTE ... CASCADE` is not supported when the type is used by a typed table (a table created with `CREATE TABLE ... OF type_name`). See issue [#30577](https://github.com/yugabyte/yugabyte-db/issues/30577).

    ```sql
    CREATE TYPE two_ints AS (a int, b int);
    CREATE TABLE typed_tbl OF two_ints;
    ALTER TYPE two_ints DROP ATTRIBUTE b CASCADE;
    ```

    ```output
    ERROR:  drop attribute on the type of a typed table is not supported yet
    ```

## Examples

Rename a type.

```plpgsql
yugabyte=# CREATE TYPE feature_enum AS ENUM ('one', 'two', 'three');
yugabyte=# ALTER TYPE feature_enum RENAME TO feature_list;
```

Add and rename enum values.

```plpgsql
yugabyte=# ALTER TYPE feature_list ADD VALUE 'four' AFTER 'three';
yugabyte=# ALTER TYPE feature_list RENAME VALUE 'one' TO 'first';
```

Move a type to a different schema.

```plpgsql
yugabyte=# CREATE SCHEMA features;
yugabyte=# ALTER TYPE feature_list SET SCHEMA features;
```

Drop an attribute of a composite type.

```plpgsql
yugabyte=# CREATE TYPE feature_struct AS (id INTEGER, name TEXT);
yugabyte=# ALTER TYPE feature_struct DROP ATTRIBUTE name;
```

```plpgsql
yugabyte=# \d feature_struct
```

```output
      Composite type "public.feature_struct"
 Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
 id     | integer |           |          |
```

## See also

- [`CREATE TYPE`](../ddl_create_type)
- [`DROP TYPE`](../ddl_drop_type)
