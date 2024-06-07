---
title: COMMIT statement [YSQL]
headerTitle: COMMIT
linkTitle: COMMIT
description: Use the COMMIT statement to commit the current transaction.
menu:
  v2.18:
    identifier: txn_commit
    parent: statements
type: docs
---

## Synopsis

Use the `COMMIT` statement to commit the current transaction. All changes made by the transaction become visible to others and are guaranteed to be durable if a crash occurs.

## Syntax

{{%ebnf%}}
  commit
{{%/ebnf%}}

## Semantics

### *commit*

```sql
COMMIT [ TRANSACTION | WORK ]
```

### WORK

Add optional keyword — has no effect.

### TRANSACTION

Add optional keyword — has no effect.

## Examples

Create a sample table.

```plpgsql
CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

Begin a transaction and insert some rows.

```plpgsql
BEGIN TRANSACTION; SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
```

```plpgsql
INSERT INTO sample(k1, k2, v1, v2) VALUES (1, 2.0, 3, 'a'), (1, 3.0, 4, 'b');
```

Start a new shell  with `ysqlsh` and begin another transaction to insert some more rows.

```plpgsql
yugabyte=# BEGIN TRANSACTION; SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
```

```plpgsql
INSERT INTO sample(k1, k2, v1, v2) VALUES (2, 2.0, 3, 'a'), (2, 3.0, 4, 'b');
```

In each shell, check the only the rows from the current transaction are visible.

1st shell.

```plpgsql
yugabyte=# SELECT * FROM sample; -- run in first shell
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  1 |  3 |  4 | b
(2 rows)
```
2nd shell

```plpgsql
yugabyte=# SELECT * FROM sample; -- run in second shell
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  2 |  2 |  3 | a
  2 |  3 |  4 | b
(2 rows)
```

Commit the first transaction and abort the second one.

```plpgsql
COMMIT TRANSACTION; -- run in first shell.
```

Abort the current transaction (from the first shell).

```plpgsql
ABORT TRANSACTION; -- run second shell.
```

In each shell check that only the rows from the committed transaction are visible.

```plpgsql
yugabyte=# SELECT * FROM sample; -- run in first shell.
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  1 |  3 |  4 | b
(2 rows)
```

```plpgsql
yugabyte=# SELECT * FROM sample; -- run in second shell.
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  1 |  3 |  4 | b
(2 rows)
```

## See also

- [`BEGIN`](../txn_begin/)
- [`START TRANSACTION`](../txn_start/)
- [`ROLLBACK`](../txn_rollback)
