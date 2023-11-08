---
title: CREATE EXTENSION statement [YSQL]
headerTitle: CREATE EXTENSION
linkTitle: CREATE EXTENSION
summary: Load an extension into a database
description: Use the CREATE EXTENSION statement to load an extension into a database.
menu:
  stable:
    identifier: ddl_create_extension
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE EXTENSION` statement to load an extension into a database.

## Syntax

{{%ebnf%}}
  create_extension
{{%/ebnf%}}

## Semantics

- `SCHEMA`, `VERSION`, and `CASCADE` may be reordered.

## Examples

```plpgsql
CREATE SCHEMA myschema;
CREATE EXTENSION pgcrypto WITH SCHEMA myschema VERSION '1.3';
```

```output
CREATE EXTENSION
```

```plpgsql
CREATE EXTENSION IF NOT EXISTS earthdistance CASCADE;
```

```output
NOTICE:  installing required extension "cube"
CREATE EXTENSION
```

## See also

- [PostgreSQL Extensions](../../../../../explore/ysql-language-features/pg-extensions/)
- [DROP EXTENSION](../ddl_drop_extension)
