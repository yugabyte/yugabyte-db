---
title: DELETE
linkTitle: DELETE
summary: DELETE
description: DELETE
menu:
  latest:
    identifier: api-ysql-commands-delete
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/dml_delete
isTocNested: true
showAsideToc: true
---

## Synopsis

DELETE removes rows that meet certain conditions, and when conditions are not provided in WHERE clause, all rows are deleted. DELETE outputs the number of rows that are being deleted.

## Syntax

### Diagram 

### Grammar
```
delete := [ WITH [ RECURSIVE ] with_query [, ...] ]
       DELETE FROM [ ONLY ] table_name [ * ] [ [ AS ] alias ]
       [ USING using_list ]
       [ WHERE condition | WHERE CURRENT OF cursor_name ]
       [ RETURNING * | output_expression [ [ AS ] output_name ] [, ...] ]
```

Where
- `with_query` specifies the subqueries that are referenced by name in the DELETE statement.

- `table_name` specifies a name of the table to be deleted.

- `alias` is the identifier of the target table within the DELETE statement. When an alias is specified, it must be used in place of the actual table in the statement.

- `output_expression` specifies the value to be returned. When the `output_expression` is referencing a column, the existing value of this column (deleted value) is used to evaluate.

## Semantics

- USING clause is not yet supported.

- While the where clause allows a wide range of operators, the exact conditions used in the where clause have significant performance considerations (especially for large datasets). WHERE clause that provides values for all columns in PRIMARY KEY or INDEX KEY has the best performance.

## Examples
Create a sample table, insert a few rows, then delete one of the inserted row.

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
postgres=# DELETE FROM sample WHERE k1 = 2 AND k2 = 3;
```

```sql
postgres=# SELECT * FROM sample ORDER BY k1;
```

```
DELETE 1
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  3 |  4 |  5 | c
(2 rows)
```

## See Also
[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[`UPDATE`](../dml_update)
[Other PostgreSQL Statements](..)
