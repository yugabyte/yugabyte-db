---
title: SHOW TRANSACTION
description: SHOW TRANSACTION
summary: SHOW TRANSACTION
menu:
  latest:
    identifier: api-ysql-commands-txn-show
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/txn_show
isTocNested: true
showAsideToc: true
---

## Synopsis

YugaByte's PostgresSQL API currently supports the following transactions-related SQL commands `BEGIN`, `ABORT`, `ROLLBACK`, `END`, `COMMIT`. Additionally the `SET` and `SHOW` commands can be used to set and, respectively, show the current transaction isolation level.

## Grammar

### Diagrams

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="339" height="34" viewbox="0 0 339 34"><path class="connector" d="M0 21h5m57 0h10m104 0h10m82 0h10m56 0h5"/><rect class="literal" x="5" y="5" width="57" height="24" rx="7"/><text class="text" x="15" y="21">SHOW</text><rect class="literal" x="72" y="5" width="104" height="24" rx="7"/><text class="text" x="82" y="21">TRANSACTION</text><rect class="literal" x="186" y="5" width="82" height="24" rx="7"/><text class="text" x="196" y="21">ISOLATION</text><rect class="literal" x="278" y="5" width="56" height="24" rx="7"/><text class="text" x="288" y="21">LEVEL</text></svg>

### Syntax

```
show ::= SHOW TRANSACTION ISOLATION LEVEL
```

## Semantics

- The `SERIALIZABLE` isolation level not yet supported. (This is currently in progress).
- Currently YugaByte will always use the snapshot isolation level internally. See more [here](../../../architecture/transactions/isolation-levels/).

## Examples

Restart the YugaByte cluster and set the flag to enable transactions for the PostgreSQL API. 

For Mac/Linux: 

```sh
$ ./bin/yb-ctl destroy; ./bin/yb-ctl create --enable_postgres
```

For Docker:

```sh
$ ./bin/yb-docker-ctl destroy; ./bin/yb-docker-ctl create --enable_postgres
```


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

[`SET`](../txn_set)
[Other PostgreSQL Statements](..)
