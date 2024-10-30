---
title: SET TRANSACTION statement [YSQL]
headerTitle: SET TRANSACTION
linkTitle: SET TRANSACTION
description: Use the `SET TRANSACTION` statement to set the current transaction isolation level.
summary: SET TRANSACTION
menu:
  v2.20:
    identifier: txn_set
    parent: statements
type: docs
---

## Synopsis

Use the `SET TRANSACTION` statement to set the current transaction isolation level.

## Syntax

{{%ebnf%}}
  set_transaction,
  transaction_mode,
  isolation_level,
  read_write_mode,
  deferrable_mode
{{%/ebnf%}}

## Semantics

Supports Serializable, Snapshot and Read Committed Isolation<sup>$</sup> using the PostgreSQL isolation level syntax of `SERIALIZABLE`, `REPEATABLE READ` and `READ COMMITTED` respectively. PostgreSQL's `READ UNCOMMITTED` also maps to Read Committed Isolation.

<sup>$</sup> Read Committed Isolation is supported only if the YB-TServer flag `yb_enable_read_committed_isolation` is set to `true`. By default this flag is `false` and in this case the Read Committed isolation level of YugabyteDB's transactional layer falls back to the stricter Snapshot Isolation (in which case `READ COMMITTED` and `READ UNCOMMITTED` of YSQL also in turn use Snapshot Isolation). Read Committed support is currently in [Tech Preview](/preview/releases/versioning/#feature-availability).

### *transaction_mode*

Set the transaction mode to one of the following.

- `ISOLATION LEVEL` clause
- Access mode
- `DEFERRABLE` mode

### ISOLATION LEVEL clause

#### SERIALIZABLE

Default in ANSI SQL standard.

#### REPEATABLE READ

Maps to Snapshot Isolation of YugabyteDB.

#### READ COMMITTED

Read Committed support is currently in [Early Access](/preview/releases/versioning/#feature-availability).

Default in PostgreSQL and YSQL.

If `yb_enable_read_committed_isolation=true`, `READ COMMITTED` is mapped to Read Committed of YugabyteDB's transactional layer (that is, a statement will see all rows that are committed before it begins). But, by default `yb_enable_read_committed_isolation=false` and in this case Read Committed of YugabyteDB's transactional layer falls back to the stricter Snapshot Isolation.

Essentially this boils down to the fact that Snapshot Isolation is the default in YSQL.

#### READ UNCOMMITTED

`READ UNCOMMITTED` maps to Read Committed of YugabyteDB's transactional layer (note that Read Committed in the transactional layer might in turn map to Snapshot Isolation if `yb_enable_read_committed_isolation=false`).

In PostgreSQL, `READ UNCOMMITTED` is mapped to `READ COMMITTED`.

### READ WRITE mode

Default.

### READ ONLY mode

The `READ ONLY` mode does not prevent all writes to disk.

When a transaction is `READ ONLY`, the following SQL statements are:

- Disallowed if the table they would write to is not a temporary table.
  - INSERT
  - UPDATE
  - DELETE
  - COPY FROM

- Always disallowed
  - COMMENT
  - GRANT
  - REVOKE
  - TRUNCATE

- Disallowed when the statement that would be executed is one of the above
  - EXECUTE
  - EXPLAIN ANALYZE

### DEFERRABLE mode

Use to defer a transaction only when both `SERIALIZABLE` and `READ ONLY` modes are also selected. If used, then the transaction may block when first acquiring its snapshot, after which it is able to run without the normal overhead of a `SERIALIZABLE` transaction and without any risk of contributing to, or being canceled by a serialization failure.

The `DEFERRABLE` mode may be useful for long-running reports or back-ups.

## Examples

Create a sample table.

```plpgsql
yugabyte=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

Begin a transaction and insert some rows.

```plpgsql
yugabyte=# BEGIN TRANSACTION; SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
```

```plpgsql
yugabyte=# INSERT INTO sample(k1, k2, v1, v2) VALUES (1, 2.0, 3, 'a'), (1, 3.0, 4, 'b');
```

Start a new shell  with `ysqlsh` and begin another transaction to insert some more rows.

```plpgsql
yugabyte=# BEGIN TRANSACTION; SET TRANSACTION ISOLATION LEVEL REPEATABLE READ;
```

```plpgsql
yugabyte=# INSERT INTO sample(k1, k2, v1, v2) VALUES (2, 2.0, 3, 'a'), (2, 3.0, 4, 'b');
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
yugabyte=# COMMIT TRANSACTION; -- run in first shell.
```

Abort the current transaction (from the first shell).

```plpgsql
yugabyte=# ABORT TRANSACTION; -- run second shell.
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

- [`SHOW TRANSACTION`](../txn_show)
