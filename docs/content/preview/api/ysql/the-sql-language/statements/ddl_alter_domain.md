---
title: ALTER DOMAIN statement [YSQL]
headerTitle: ALTER DOMAIN
linkTitle: ALTER DOMAIN
description: Use the ALTER DOMAIN statement to change the definition of a domain.
menu:
  preview:
    identifier: ddl_alter_domain
    parent: statements
aliases:
  - /preview/api/ysql/commands/ddl_alter_domain/
type: docs
---

## Synopsis

Use the `ALTER DOMAIN` statement to change the definition of a domain.

## Syntax

{{%ebnf%}}
  alter_domain_default,
  alter_domain_rename
{{%/ebnf%}}

## Semantics

### SET DEFAULT | DROP DEFAULT

Set or remove the default value for a domain.

### RENAME

Change the name of the domain.

### *name*

Specify the name of the domain. An error is raised if DOMAIN `name` does not exist or DOMAIN `new_name` already exists.

## Examples

```plpgsql
yugabyte=# CREATE DOMAIN idx DEFAULT 5 CHECK (VALUE > 0);
```

```plpgsql
yugabyte=# ALTER DOMAIN idx DROP DEFAULT;
```

```plpgsql
yugabyte=# ALTER DOMAIN idx RENAME TO idx_new;
```

```plpgsql
yugabyte=# DROP DOMAIN idx_new;
```

## See also

- [`CREATE DOMAIN`](../ddl_create_domain)
- [`DROP DOMAIN`](../ddl_drop_domain)
