---
title: COMMIT
description: COMMIT
summary: COMMIT
menu:
  latest:
    identifier: api-ysql-commands-txn-commit
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/txn_commit
isTocNested: true
showAsideToc: true
---

## Synopsis

`COMMIT` commit the current transaction. All changes made by the transaction become visible to others and are guaranteed to be durable if a crash occurs.

## Grammar

### Diagrams

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="289" height="155" viewbox="0 0 289 155"><path class="connector" d="M0 22h25m60 0h43m-113 25q0 5 5 5h5m83 0h5q5 0 5-5m-103 30q0 5 5 5h5m59 0h29q5 0 5-5m-103 30q0 5 5 5h5m46 0h42q5 0 5-5m-108-85q5 0 5 5v110q0 5 5 5h5m69 0h19q5 0 5-5v-110q0-5 5-5m5 0h30m106 0h20m-136 25q0 5 5 5h5m57 0h54q5 0 5-5m-131-25q5 0 5 5v33q0 5 5 5h116q5 0 5-5v-33q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="60" height="25" rx="7"/><text class="text" x="35" y="22">ABORT</text><rect class="literal" x="25" y="35" width="83" height="25" rx="7"/><text class="text" x="35" y="52">ROLLBACK</text><rect class="literal" x="25" y="65" width="59" height="25" rx="7"/><text class="text" x="35" y="82">BEGIN</text><rect class="literal" x="25" y="95" width="46" height="25" rx="7"/><text class="text" x="35" y="112">END</text><rect class="literal" x="25" y="125" width="69" height="25" rx="7"/><text class="text" x="35" y="142">COMMIT</text><rect class="literal" x="158" y="5" width="106" height="25" rx="7"/><text class="text" x="168" y="22">TRANSACTION</text><rect class="literal" x="158" y="35" width="57" height="25" rx="7"/><text class="text" x="168" y="52">WORK</text></svg>

### Syntax

```
commit_transaction ::= { 'COMMIT' } [ 'TRANSACTION' | 'WORK' ] ;
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

[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[Other PostgreSQL Statements](..)
