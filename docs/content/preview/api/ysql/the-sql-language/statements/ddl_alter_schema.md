---
title: ALTER SCHEMA statement [YSQL]
headerTitle: ALTER SCHEMA
linkTitle: ALTER SCHEMA
description: Use the ALTER SCHEMA statement to change the definition of a schema.
menu:
  preview:
    identifier: ddl_alter_schema
    parent: statements
aliases:
  - /preview/api/ysql/commands/ddl_alter_schema/
type: docs
---

## Synopsis

Use the `ALTER SCHEMA` statement to change the definition of a schema.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link active" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/alter_schema.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade show active" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/alter_schema.diagram.md" %}}
  </div>
</div>

## Semantics

* ALTER SCHEMA changes the definition of a schema.
* In order to use `ALTER SCHEMA`, you need to be the owner of the schema.
* Renaming a schema requires having the `CREATE` privilege for the database.
* If you want to change the owner, you must also be a direct or indirect member of the new owning role, and you need to have the `CREATE` privilege for the database. (It's worth noting that superusers possess these privileges by default.)

### *alter_schema*

#### ALTER SCHEMA *schema_name* 

Specify the name of the schema (*schema_name*). An error is raised if a schema with that name does not exist in the current database.

#### schema_name

The name of the schema.

#### RENAME TO *new_name*

Rename the schema.

#### new_name

Schema names must not begin with `pg_`. The attempt to create a schema with such a name, or to rename an existing schema to have such a name, causes an error.

#### OWNER TO  (*new_owner* | *CURRENT_USER* | *SESSION_USER*)

Change the owner of the schema.

#### new_owner

The new owner of the schema.

#### CURRENT_USER

Username of current execution context.

#### SESSION_USER

Username of current session.

## Examples

Create a simple schema.

```plpgsql
yugabyte=# CREATE SCHEMA schema22;
```

```
CREATE SCHEMA
```

Rename the schema.

```plpgsql
yugabyte=# ALTER SCHEMA schema22 RENAME TO schema25;
```

```
ALTER SCHEMA
```

```plpgsql
yugabyte=# \dn
```

```
   List of schemas
   Name   |  Owner   
----------+----------
 public   | postgres
 schema25 | yugabyte
(2 rows)

```

Change the owner of the schema.

```plpgsql
yugabyte=# ALTER SCHEMA schema25 OWNER TO postgres;
```

```
ALTER SCHEMA
```

```plpgsql
yugabyte=# \dn
```

```
   List of schemas
   Name   |  Owner   
----------+----------
 public   | postgres
 schema25 | postgres
(2 rows)
```

## See also

- [`CREATE SCHEMA`](../ddl_create_schema)
- [`DROP SCHEMA`](../ddl_drop_schema)
