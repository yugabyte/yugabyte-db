---
title: DROP SCHEMA statement [YSQL]
headerTitle: DROP SCHEMA
linkTitle: DROP SCHEMA
description: Use the DROP SCHEMA statement to remove a schema and all of its associated objects from the system.
menu:
  preview:
    identifier: ddl_drop_schema
    parent: statements
aliases:
  - /preview/api/ysql/commands/ddl_drop_schema/
type: docs
---

## Synopsis

Use the `DROP SCHEMA` statement to remove a schema and all of its associated objects from the system. This is an irreversible statement. 

## Syntax

{{%ebnf%}}
  drop_schema
{{%/ebnf%}}

## Semantics

- `DROP SCHEMA... CASCADE` executes in a single transaction so that _either_ it has no effect (if it's interrupted) _or_ the nominated schema together with all the objects in it are dropped.

### *drop_schema*

#### DROP SCHEMA [ IF EXISTS ] *schema_name*

Remove a schema from the database. The schema can only be dropped by its owner or a superuser.

### *schema_name*

Specify the name of the schema.

### *CASCADE*

Remove a schema and all associated objects.  All objects that are associated with `schema_name` such as tables will be invalidated after the drop statement is completed.

### *RESTRICT*

Refuse to drop the schema if it contains any objects. This is the default.

## Example

Create a schema with a table:

```sql
CREATE SCHEMA sch1;
CREATE TABLE sch1.t1(id BIGSERIAL PRIMARY KEY);
```

Try to drop the schema:

```sql
DROP SCHEMA sch1;
ERROR:  cannot drop schema sch1 because other objects depend on it
DETAIL:  table sch1.t1 depends on schema sch1
HINT:  Use DROP ... CASCADE to drop the dependent objects too.
```

Drop a schema with `CASCADE`:

```sql
DROP SCHEMA sch1 CASCADE;
NOTICE:  drop cascades to table sch1.t1
DROP SCHEMA
```

## See also

- [`CREATE SCHEMA`](../ddl_create_schema)
- [`ALTER SCHEMA`](../ddl_alter_schema)
