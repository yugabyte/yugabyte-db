---
title: CREATE TABLE AS
linkTitle: CREATE TABLE AS
summary: Create a new table from a query result
description: CREATE TABLE AS
menu:
  latest:
    identifier: api-ysql-commands-create-table-as
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/ddl_create_table_as
isTocNested: true
showAsideToc: true
---

## Synopsis

`CREATE TABLE AS` command create new table using the output of a subquery.

## Syntax

### Diagram 

### Grammar
```
create_table_as ::= CREATE TABLE [ IF NOT EXISTS ] table_name [ (column_name [, ...] ) ]
                           AS query [ WITH [ NO ] DATA ]
```

Where
- table_name specifies the name of the table to be created.
- column_name specifies the name of a column in the new table. When not specified, column names are the selected names of the query.

## Semantics

- YugaByte may extend syntax to allow specifying PRIMARY KEY for `CREATE TABLE AS` command.

## Examples
```sql
postgres=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

```sql
postgres=# INSERT INTO sample VALUES (1, 2.0, 3, 'a'), (2, 3.0, 4, 'b'), (3, 4.0, 5, 'c');
```

```sql
postgres=# CREATE TABLE selective_sample SELECT * FROM sample WHERE k1 > 1;
```

```sql
postgres=# SELECT * FROM selective_sample ORDER BY k1;
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  2 |  3 |  4 | b
  3 |  4 |  5 | c
(2 rows)
```

## See Also
[`CREATE TABLE`](../dml_create_table)
[Other PostgreSQL Statements](..)
