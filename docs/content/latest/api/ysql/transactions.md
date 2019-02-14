---
title: Transactions
description: Transactions
summary: Overview of transaction-related commands.
menu:
  latest:
    identifier: api-postgresql-transactions
    parent: api-postgresql
    weight: 3400
aliases:
  - /latest/api/postgresql/transactions
  - /latest/api/ysql/transactions
isTocNested: true
showAsideToc: true
---

## Synopsis

YugaByte's PostgresSQL API currently supports the following transactions-related SQL commands `BEGIN`, `ABORT`, `ROLLBACK`, `END`, `COMMIT`.
Additionally the `SET` and `SHOW` commands can be used to set and, respectively, show the current transaction isolation level.

## Grammar

### Diagrams

#### transaction

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="289" height="155" viewbox="0 0 289 155"><path class="connector" d="M0 22h25m60 0h43m-113 25q0 5 5 5h5m83 0h5q5 0 5-5m-103 30q0 5 5 5h5m59 0h29q5 0 5-5m-103 30q0 5 5 5h5m46 0h42q5 0 5-5m-108-85q5 0 5 5v110q0 5 5 5h5m69 0h19q5 0 5-5v-110q0-5 5-5m5 0h30m106 0h20m-136 25q0 5 5 5h5m57 0h54q5 0 5-5m-131-25q5 0 5 5v33q0 5 5 5h116q5 0 5-5v-33q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="60" height="25" rx="7"/><text class="text" x="35" y="22">ABORT</text><rect class="literal" x="25" y="35" width="83" height="25" rx="7"/><text class="text" x="35" y="52">ROLLBACK</text><rect class="literal" x="25" y="65" width="59" height="25" rx="7"/><text class="text" x="35" y="82">BEGIN</text><rect class="literal" x="25" y="95" width="46" height="25" rx="7"/><text class="text" x="35" y="112">END</text><rect class="literal" x="25" y="125" width="69" height="25" rx="7"/><text class="text" x="35" y="142">COMMIT</text><rect class="literal" x="158" y="5" width="106" height="25" rx="7"/><text class="text" x="168" y="22">TRANSACTION</text><rect class="literal" x="158" y="35" width="57" height="25" rx="7"/><text class="text" x="168" y="52">WORK</text></svg>

#### set

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="551" height="95" viewbox="0 0 551 95"><path class="connector" d="M0 22h5m43 0h10m106 0h10m87 0h10m58 0h30m53 0h10m104 0h20m-197 25q0 5 5 5h5m53 0h10m86 0h23q5 0 5-5m-192-25q5 0 5 5v50q0 5 5 5h5m97 0h10m53 0h12q5 0 5-5v-50q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="43" height="25" rx="7"/><text class="text" x="15" y="22">SET</text><rect class="literal" x="58" y="5" width="106" height="25" rx="7"/><text class="text" x="68" y="22">TRANSACTION</text><rect class="literal" x="174" y="5" width="87" height="25" rx="7"/><text class="text" x="184" y="22">ISOLATION</text><rect class="literal" x="271" y="5" width="58" height="25" rx="7"/><text class="text" x="281" y="22">LEVEL</text><rect class="literal" x="359" y="5" width="53" height="25" rx="7"/><text class="text" x="369" y="22">READ</text><rect class="literal" x="422" y="5" width="104" height="25" rx="7"/><text class="text" x="432" y="22">UNCOMMITTED</text><rect class="literal" x="359" y="35" width="53" height="25" rx="7"/><text class="text" x="369" y="52">READ</text><rect class="literal" x="422" y="35" width="86" height="25" rx="7"/><text class="text" x="432" y="52">COMMITTED</text><rect class="literal" x="359" y="65" width="97" height="25" rx="7"/><text class="text" x="369" y="82">REPEATABLE</text><rect class="literal" x="466" y="65" width="53" height="25" rx="7"/><text class="text" x="476" y="82">READ</text></svg>

#### show

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="349" height="35" viewbox="0 0 349 35"><path class="connector" d="M0 22h5m58 0h10m106 0h10m87 0h10m58 0h5"/><rect class="literal" x="5" y="5" width="58" height="25" rx="7"/><text class="text" x="15" y="22">SHOW</text><rect class="literal" x="73" y="5" width="106" height="25" rx="7"/><text class="text" x="83" y="22">TRANSACTION</text><rect class="literal" x="189" y="5" width="87" height="25" rx="7"/><text class="text" x="199" y="22">ISOLATION</text><rect class="literal" x="286" y="5" width="58" height="25" rx="7"/><text class="text" x="296" y="22">LEVEL</text></svg>

### Syntax

```
transaction ::= { 'ABORT' | 'ROLLBACK' | 'BEGIN' | 'END' | 'COMMIT' } [ 'TRANSACTION' | 'WORK' ] ;

set ::= SET TRANSACTION ISOLATION LEVEL { READ UNCOMMITTED | READ COMMITTED | REPEATABLE READ }
show ::= SHOW TRANSACTION ISOLATION LEVEL
```

## Semantics

- `BEGIN` starts a new transaction with the default (or given) isolation level.
- `END` or `COMMIT` commit the current transaction. All changes made by the transaction become visible to others and are guaranteed to be durable if a crash occurs.
- `ABORT` or `ROLLBACK` roll back the current transactions. All changes included in this transactions will be discarded.

- `SET` and `SHOW` commands can be used to set the current transaction isolation level.
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
