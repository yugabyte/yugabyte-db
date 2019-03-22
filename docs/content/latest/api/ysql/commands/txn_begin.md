---
title: BEGIN TRANSACTION
description: BEGIN TRANSACTION
summary: BEGIN TRANSACTION
menu:
  latest:
    identifier: api-ysql-commands-txn-begin
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/transactions/
isTocNested: true
showAsideToc: true
---

## Synopsis

`BEGIN` starts a new transaction with the default (or given) isolation level.

## Grammar

### Diagrams
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="221" height="68" viewbox="0 0 221 68"><path class="connector" d="M0 21h5m57 0h30m104 0h20m-134 24q0 5 5 5h5m55 0h54q5 0 5-5m-129-24q5 0 5 5v32q0 5 5 5h114q5 0 5-5v-32q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="57" height="24" rx="7"/><text class="text" x="15" y="21">BEGIN</text><rect class="literal" x="92" y="5" width="104" height="24" rx="7"/><text class="text" x="102" y="21">TRANSACTION</text><rect class="literal" x="92" y="34" width="55" height="24" rx="7"/><text class="text" x="102" y="50">WORK</text></svg>

### Syntax

```
transaction ::= 'BEGIN' [ 'TRANSACTION' | 'WORK' ] ;
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
