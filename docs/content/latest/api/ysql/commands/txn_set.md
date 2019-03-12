---
title: SET TRANSACTION
description: SET TRANSACTION
summary: SET TRANSACTION
menu:
  latest:
    identifier: api-ysql-commands-txn-set
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/txn_set
isTocNested: true
showAsideToc: true
---

## Synopsis

`SET TRANSACTION` command sets the current transaction isolation level.

## Grammar

### Diagrams

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="547" height="92" viewbox="0 0 547 92"><path class="connector" d="M0 21h5m43 0h10m104 0h10m82 0h10m56 0h30m53 0h10m109 0h20m-202 24q0 5 5 5h5m53 0h10m91 0h23q5 0 5-5m-197-24q5 0 5 5v48q0 5 5 5h5m97 0h10m53 0h17q5 0 5-5v-48q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="43" height="24" rx="7"/><text class="text" x="15" y="21">SET</text><rect class="literal" x="58" y="5" width="104" height="24" rx="7"/><text class="text" x="68" y="21">TRANSACTION</text><rect class="literal" x="172" y="5" width="82" height="24" rx="7"/><text class="text" x="182" y="21">ISOLATION</text><rect class="literal" x="264" y="5" width="56" height="24" rx="7"/><text class="text" x="274" y="21">LEVEL</text><rect class="literal" x="350" y="5" width="53" height="24" rx="7"/><text class="text" x="360" y="21">READ</text><rect class="literal" x="413" y="5" width="109" height="24" rx="7"/><text class="text" x="423" y="21">UNCOMMITTED</text><rect class="literal" x="350" y="34" width="53" height="24" rx="7"/><text class="text" x="360" y="50">READ</text><rect class="literal" x="413" y="34" width="91" height="24" rx="7"/><text class="text" x="423" y="50">COMMITTED</text><rect class="literal" x="350" y="63" width="97" height="24" rx="7"/><text class="text" x="360" y="79">REPEATABLE</text><rect class="literal" x="457" y="63" width="53" height="24" rx="7"/><text class="text" x="467" y="79">READ</text></svg>

### Syntax

```
set ::= SET TRANSACTION ISOLATION LEVEL { READ UNCOMMITTED | READ COMMITTED | REPEATABLE READ }
```

## Semantics

- The `SERIALIZABLE` isolation level not yet supported. (This is currently in progress).
- Currently YugaByte will always use the snapshot isolation level internally. See more [here](../../../architecture/transactions/isolation-levels/).

## Examples

Create a sample table.

```sql
postgres=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```


Begin a transaction and insert some rows.

```sql
postgres=# BEGIN TRANSACTION; SET TRANSACTION ISOLATION LEVEL REPEATABLE READ; 
```

```sql
postgres=# INSERT INTO sample(k1, k2, v1, v2) VALUES (1, 2.0, 3, 'a'), (1, 3.0, 4, 'b');
```

Start a new shell  with `psql` and begin another transaction to insert some more rows.

```sql
postgres=# BEGIN TRANSACTION; SET TRANSACTION ISOLATION LEVEL REPEATABLE READ; 
```

```sql
postgres=# INSERT INTO sample(k1, k2, v1, v2) VALUES (2, 2.0, 3, 'a'), (2, 3.0, 4, 'b');
```

In each shell, check the only the rows from the current transaction are visible.

1st shell.

```sql
postgres=# SELECT * FROM sample; -- run in first shell
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  1 |  3 |  4 | b
(2 rows)
```
2nd shell

```sql
postgres=# SELECT * FROM sample; -- run in second shell
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  2 |  2 |  3 | a
  2 |  3 |  4 | b
(2 rows)
```

Commit the first transaction and abort the second one.

```sql
postgres=# COMMIT TRANSACTION; -- run in first shell.
```

Abort the current transaction (from the first shell).

```sql
postgres=# ABORT TRANSACTION; -- run second shell.
```

In each shell check that only the rows from the committed transaction are visible.

```sql
postgres=# SELECT * FROM sample; -- run in first shell.
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  1 |  3 |  4 | b
(2 rows)
```

```sql
postgres=# SELECT * FROM sample; -- run in second shell.
```

```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  1 |  3 |  4 | b
(2 rows)
```

## See Also

[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[Other YSQL Statements](..)
