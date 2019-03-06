---
title: TRUNCATE
linkTitle: TRUNCATE
summary: Clear all rows in a table
description: TRUNCATE
menu:
  latest:
    identifier: api-ysql-commands-truncate
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/ddl_truncate
isTocNested: true
showAsideToc: true
---

## Synopsis

`TRUNCATE` command clears all rows in a table.

## Syntax

### Diagram 

### Grammar
```
truncate_stmt ::= TRUNCATE [ TABLE ] [ ONLY ] name [ * ] [, ... ]
```

Where

- `name` specifies the table to be truncated.

## Semantics

- TRUNCATE acquires ACCESS EXCLUSIVE lock on the tables to be truncated. The ACCESS EXCLUSIVE locking option is not yet fully supported.
- TRUNCATE is not supported for foreign tables.

## Examples
```sql
postgres=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

```sql
postgres=# INSERT INTO sample VALUES (1, 2.0, 3, 'a'), (2, 3.0, 4, 'b'), (3, 4.0, 5, 'c');
```

```sql
postgres=# SELECT * FROM sample ORDER BY k1;
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  2 |  3 |  4 | b
  3 |  4 |  5 | c
(3 rows)
```

```sql
postgres=# TRUNCATE sample;
```

```sql
postgres=# SELECT * FROM sample;
```

```
 k1 | k2 | v1 | v2
----+----+----+----
(0 rows)
```

## See Also
[Other PostgreSQL Statements](..)
