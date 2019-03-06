---
title: DEALLOCATE
linkTitle: DEALLOCATE
summary: Deallocate a prepared statement
description: DEALLOCATE
menu:
  latest:
    identifier: api-ysql-commands-deallocate
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/perf_deallocate
isTocNested: true
showAsideToc: true
---

## Synopsis

`DEALLOCATE` command deallocates a previously-prepared statement.

## Syntax

### Diagram 

### Grammar
```
deallocate_stmt ::= DEALLOCATE [ PREPARE ] { name | ALL }
```

Where
- name specifies the prepared statement to deallocate.

## Semantics

- `ALL` option is to deallocate all prepared statements.

## Examples
Prepare and deallocate an insert statement.

```sql
postgres=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

```sql
postgres=# PREPARE ins (bigint, double precision, int, text) AS 
               INSERT INTO sample(k1, k2, v1, v2) VALUES ($1, $2, $3, $4);
```

```sql
postgres=# DEALLOCATE ins;
```

## See Also
[Other PostgreSQL Statements](..)
