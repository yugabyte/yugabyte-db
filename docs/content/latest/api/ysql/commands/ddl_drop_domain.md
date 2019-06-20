---
title: DROP DOMAIN
summary: Remove a domain
description: DROP DOMAIN
menu:
  latest:
    identifier: api-ysql-commands-drop-domain
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/ddl_drop_domain/
isTocNested: true
showAsideToc: true
---

## Synopsis
The `DROP DOMAIN` command removes a domain from the database.

## Syntax

### Diagrams

### drop_domain

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="626" height="70" viewbox="0 0 626 70"><path class="connector" d="M0 22h5m53 0h10m70 0h30m32 0h10m64 0h20m-141 0q5 0 5 5v8q0 5 5 5h116q5 0 5-5v-8q0-5 5-5m5 0h10m106 0h30m32 0h20m-67 0q5 0 5 5v8q0 5 5 5h42q5 0 5-5v-8q0-5 5-5m5 0h30m77 0h22m-109 25q0 5 5 5h5m79 0h5q5 0 5-5m-104-25q5 0 5 5v33q0 5 5 5h89q5 0 5-5v-33q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="53" height="25" rx="7"/><text class="text" x="15" y="22">DROP</text><rect class="literal" x="68" y="5" width="70" height="25" rx="7"/><text class="text" x="78" y="22">DOMAIN</text><rect class="literal" x="168" y="5" width="32" height="25" rx="7"/><text class="text" x="178" y="22">IF</text><rect class="literal" x="210" y="5" width="64" height="25" rx="7"/><text class="text" x="220" y="22">EXISTS</text><a xlink:href="../../grammar_diagrams#domain-name"><rect class="rule" x="304" y="5" width="106" height="25"/><text class="text" x="314" y="22">domain_name</text></a><a xlink:href="../../grammar_diagrams#..."><rect class="rule" x="440" y="5" width="32" height="25"/><text class="text" x="450" y="22">...</text></a><rect class="literal" x="522" y="5" width="77" height="25" rx="7"/><text class="text" x="532" y="22">CASCADE</text><rect class="literal" x="522" y="35" width="79" height="25" rx="7"/><text class="text" x="532" y="52">RESTRICT</text></svg>

### Grammar
```
drop_domain ::= DROP DOMAIN [IF EXISTS ] name [, ...]  [ CASCADE | RESTRICT ]
```

Where

- `IF EXISTS` does not throw an error if domain does not exist.
- `name` is the name of the existing domain.
- `CASCADE` automatically drops objects that depend on the domain such as table columns using the domain data type and, in turn, all other objects that depend on those objects.
- `RESTRICT` refuses to drop the domain if objects depend on it (default).

## Semantics

- An error is raised if the specified domain does not exist (unless `IF EXISTS` is set).
- An error is raised if any objects depend on this domain (unless `CASCADE` is set).

## Examples
Example 1

```sql
postgres=# CREATE DOMAIN idx DEFAULT 5 CHECK (VALUE > 0);
```

```sql
postgres=# DROP DOMAIN idx;
```

Example 2

```sql
postgres=# CREATE DOMAIN idx DEFAULT 5 CHECK (VALUE > 0);
```

```sql
postgres=# CREATE TABLE t (k idx primary key);
```

```sql
postgres=# DROP DOMAIN idx CASCADE;
```

## See Also

[`CREATE DOMAIN`](../ddl_create_domain)
[`ALTER DOMAIN`](../ddl_alter_domain)
[Other YSQL Statements](..)
