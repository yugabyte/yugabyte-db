---
title: SAVEPOINT statement [YSQL]
headerTitle: SAVEPOINT
linkTitle: SAVEPOINT
description: Use the `SAVEPOINT` statement to start a subtransaction within the current transaction.
menu:
  v2.20:
    identifier: savepoint_create
    parent: statements
type: docs
---

## Synopsis

Use the `SAVEPOINT` statement to define a new savepoint within the current transaction. A savepoint is a special "checkpoint" inside a transaction that allows all commands that are executed after it was established to be rolled back, restoring the transaction state to what it was at the time of the savepoint.

## Syntax

{{%ebnf%}}
  savepoint_create
{{%/ebnf%}}

## Semantics

### *begin*

```plpgsql
SAVEPOINT name
```

#### NAME

The name of your savepoint.

## Examples

Create a sample table.

```plpgsql
CREATE TABLE sample(k int PRIMARY KEY, v int);
```

Begin a transaction and insert some rows.

```plpgsql
BEGIN TRANSACTION;
INSERT INTO sample(k, v) VALUES (1, 2);
SAVEPOINT test;
INSERT INTO sample(k, v) VALUES (3, 4);
```

Now, check the rows in this table:

```plpgsql
SELECT * FROM sample;
```

```output
 k  | v
----+----
  1 |  2
  3 |  4
(2 rows)
```

And then, rollback to savepoint `test` and check the rows again. Note that the second row does not appear:

```plpgsql
ROLLBACK TO test;
SELECT * FROM sample;
```

```output
 k  | v
----+----
  1 |  2
(1 row)
```

We can even add a new row at this point. If we then commit the transaction, only the first and third row inserted will persist:

```plpgsql
INSERT INTO sample(k, v) VALUES (5, 6);
COMMIT;
SELECT * FROM SAMPLE;
```

```output
 k  | v
----+----
  1 |  2
  5 |  6
(2 rows)
```

## See also

- [`ROLLBACK TO`](../savepoint_rollback)
- [`RELEASE`](../savepoint_release)
