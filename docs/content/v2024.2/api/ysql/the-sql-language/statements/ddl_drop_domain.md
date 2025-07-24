---
title: DROP DOMAIN statement [YSQL]
headerTitle: DROP DOMAIN
linkTitle: DROP DOMAIN
description: Use the DROP DOMAIN statement to remove a domain from the database.
menu:
  v2024.2_api:
    identifier: ddl_drop_domain
    parent: statements
type: docs
---

## Synopsis

Use the `DROP DOMAIN` statement to remove a domain from the database.

## Syntax

{{%ebnf%}}
  drop_domain
{{%/ebnf%}}

## Semantics

### *drop_domain*

#### *name*

Specify the name of the domain. An error is raised if the specified domain does not exist (unless `IF EXISTS` is set). An error is raised if any objects depend on this domain (unless `CASCADE` is set).

### IF EXISTS

Do not throw an error if the domain does not exist.

### CASCADE

Automatically drop objects that depend on the domain such as table columns using the domain data type and, in turn, all other objects that depend on those objects.

### RESTRICT

Refuse to drop the domain if objects depend on it (default).

## Examples

### Example 1

```plpgsql
yugabyte=# CREATE DOMAIN idx DEFAULT 5 CHECK (VALUE > 0);
```

```plpgsql
yugabyte=# DROP DOMAIN idx;
```

### Example 2

```plpgsql
yugabyte=# CREATE DOMAIN idx DEFAULT 5 CHECK (VALUE > 0);
```

```plpgsql
yugabyte=# CREATE TABLE t (k idx primary key);
```

```plpgsql
yugabyte=# DROP DOMAIN idx CASCADE;
```

## See also

- [`ALTER DOMAIN`](../ddl_alter_domain)
- [`CREATE DOMAIN`](../ddl_create_domain)
