---
title: ALTER TYPE statement [YSQL]
headerTitle: ALTER TYPE
linkTitle: ALTER TYPE
description: Use the ALTER TYPE statement to change the definition of a user-defined type.
menu:
  v2025.1_api:
    identifier: ddl_alter_type
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER TYPE` statement to change the definition of a user-defined type, such as renaming the type, changing its owner or schema, or adding or renaming the values of an enumerated type.

## Syntax

{{%ebnf%}}
  alter_type_rename,
  alter_type_owner,
  alter_type_set_schema,
  alter_type_rename_value,
  alter_type_add_value
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

## Limitations

- The attribute forms of `ALTER TYPE` on composite types — `ADD ATTRIBUTE`, `DROP ATTRIBUTE`, `ALTER ATTRIBUTE ... TYPE`, and `RENAME ATTRIBUTE` — are not supported. See issue [#1893](https://github.com/yugabyte/yugabyte-db/issues/1893). (`DROP ATTRIBUTE` is supported in v2025.2.5.0 and later.)

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

## See also

- [`CREATE TYPE`](../ddl_create_type)
- [`DROP TYPE`](../ddl_drop_type)
