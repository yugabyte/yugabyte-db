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

`SET` command is used to set the current transaction isolation level.

## Grammar

### Diagrams

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="551" height="95" viewbox="0 0 551 95"><path class="connector" d="M0 22h5m43 0h10m106 0h10m87 0h10m58 0h30m53 0h10m104 0h20m-197 25q0 5 5 5h5m53 0h10m86 0h23q5 0 5-5m-192-25q5 0 5 5v50q0 5 5 5h5m97 0h10m53 0h12q5 0 5-5v-50q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="43" height="25" rx="7"/><text class="text" x="15" y="22">SET</text><rect class="literal" x="58" y="5" width="106" height="25" rx="7"/><text class="text" x="68" y="22">TRANSACTION</text><rect class="literal" x="174" y="5" width="87" height="25" rx="7"/><text class="text" x="184" y="22">ISOLATION</text><rect class="literal" x="271" y="5" width="58" height="25" rx="7"/><text class="text" x="281" y="22">LEVEL</text><rect class="literal" x="359" y="5" width="53" height="25" rx="7"/><text class="text" x="369" y="22">READ</text><rect class="literal" x="422" y="5" width="104" height="25" rx="7"/><text class="text" x="432" y="22">UNCOMMITTED</text><rect class="literal" x="359" y="35" width="53" height="25" rx="7"/><text class="text" x="369" y="52">READ</text><rect class="literal" x="422" y="35" width="86" height="25" rx="7"/><text class="text" x="432" y="52">COMMITTED</text><rect class="literal" x="359" y="65" width="97" height="25" rx="7"/><text class="text" x="369" y="82">REPEATABLE</text><rect class="literal" x="466" y="65" width="53" height="25" rx="7"/><text class="text" x="476" y="82">READ</text></svg>

### Syntax

```
set ::= SET TRANSACTION ISOLATION LEVEL { READ UNCOMMITTED | READ COMMITTED | REPEATABLE READ }
```

## Semantics

- The `SERIALIZABLE` isolation level not yet supported. (This is currently in progress).
- Currently YugaByte will always use the snapshot isolation level internally. See more [here](../../../architecture/transactions/isolation-levels/).

## Examples

Restart the YugaByte cluster and set the flag to enable transactions for the PostgreSQL API. 

For Mac/Linux: 

```sh
$ export YB_PG_TRANSACTIONS_ENABLED=1; ./bin/yb-ctl destroy; ./bin/yb-ctl create --enable_postgres
```

For Docker:

```sh
$ export YB_PG_TRANSACTIONS_ENABLED=1; ./bin/yb-docker-ctl destroy; ./bin/yb-docker-ctl create --enable_postgres
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

[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[Other PostgreSQL Statements](..)
