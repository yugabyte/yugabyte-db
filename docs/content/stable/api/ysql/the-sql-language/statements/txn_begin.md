---
title: BEGIN statement [YSQL]
headerTitle: BEGIN
linkTitle: BEGIN
description: Use the `BEGIN` statement to start a transaction with the default (or specified) isolation level.
menu:
  stable:
    identifier: txn_begin
    parent: statements
type: docs
---

## Synopsis

Use the `BEGIN` statement to start a transaction with the default (or specified) isolation level.

## Syntax

{{%ebnf%}}
  begin,
  transaction_mode,
  isolation_level,
  read_write_mode,
  deferrable_mode
{{%/ebnf%}}

## Semantics

### *begin*

```plpgsql
BEGIN [ TRANSACTION | WORK ] [ transaction_mode [ ... ] ]
```

#### WORK

Add optional keyword — has no effect.

#### TRANSACTION

Add optional keyword — has no effect.

### *transaction_mode*

Supports isolation level, read write, and deferrable modes using the transaction mode syntax of `ISOLATION LEVEL`, `READ ONLY`, `READ WRITE`, and `[NOT] DEFERRABLE`.

For isolation level, you can specify Serializable, Snapshot, and Read Committed {{<badge/ea>}} Isolation using the PostgreSQL isolation level syntax of `SERIALIZABLE`, `REPEATABLE READ`, and `READ COMMITTED` respectively. PostgreSQL's `READ UNCOMMITTED` also maps to Read Committed Isolation.

<sup>$</sup> Read Committed support is currently in [Early Access](/preview/releases/versioning/#feature-availability). Read Committed Isolation is supported only if the YB-TServer flag `yb_enable_read_committed_isolation` is set to `true`. By default this flag is `false` and in this case the Read Committed isolation level of YugabyteDB's transactional layer falls back to the stricter Snapshot Isolation (in which case `READ COMMITTED` and `READ UNCOMMITTED` of YSQL also in turn use Snapshot Isolation).

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
BEGIN TRANSACTION; SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
```

```plpgsql
INSERT INTO sample(k1, k2, v1, v2) VALUES (2, 2.0, 3, 'a'), (2, 3.0, 4, 'b');
```

In each shell, check the only the rows from the current transaction are visible.

1st shell.

```plpgsql
yugabyte=# SELECT * FROM sample; -- run in first shell
```

```output
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

```output
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

```output
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  1 |  3 |  4 | b
(2 rows)
```

```plpgsql
yugabyte=# SELECT * FROM sample; -- run in second shell.
```

```output
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  1 |  3 |  4 | b
(2 rows)
```

Start a `READ WRITE` transaction using `SERIALIZABLE` isolation level and making foreign keys `DEFERRABLE`:

```plpgsql
yugabyte=# BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE READ WRITE DEFERRABLE;
yugabyte=# -- run queries
yugabyte=# COMMIT;
```

## See also

- [`ABORT`](../txn_abort)
- [`COMMIT`](../txn_commit)
- [`END`](../txn_end)
- [`ROLLBACK`](../txn_rollback)
- [`SET TRANSACTION`](../txn_set)
- [`START TRANSACTION`](../txn_start)
