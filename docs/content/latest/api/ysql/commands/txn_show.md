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

YSQL API currently supports the following transactions-related SQL statements: `BEGIN`, `ABORT`, `ROLLBACK`, `END`, `COMMIT`. Additionally, the `SET` and `SHOW` statements can be used to set and, respectively, show the current transaction isolation level.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <i class="fas fa-file-alt" aria-hidden="true"></i>
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <i class="fas fa-project-diagram" aria-hidden="true"></i>
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
    {{% includeMarkdown "../syntax_resources/commands/show_transaction.grammar.md" /%}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
    {{% includeMarkdown "../syntax_resources/commands/show_transaction.diagram.md" /%}}
  </div>
</div>

## Semantics

Supports both Serializable and Snapshot Isolation using the PostgreSQL isolation level syntax of `SERIALIZABLE` and `REPEATABLE READS` respectively. Even `READ COMMITTED` and `READ UNCOMMITTED` isolation levels are mapped to Snapshot Isolation.

### ISOLATION LEVEL

#### SERIALIZABLE

Default in ANSI SQL standard.

#### REPEATABLE READ

Also referred to as "snapshot isolation" in YugaByte DB.
Default in YugaByte DB.

#### READ COMMITTED

A statement can only see rows committed before it begins.

`READ_COMMITTED` is mapped to `REPEATABLE_READ`.

Default in PostgreSQL.

#### READ UNCOMMITTED

`READ_UNCOMMITTED` is mapped to `REPEATABLE_READ`.

In PostgreSQL, `READ_UNCOMMITTED` is mapped to `READ_COMMITTED`.

### READ WRITE

### READ ONLY

### DEFERRABLE

## Examples

Note that the `SERIALIZABLE` isolation level support was added in [v1.2.6](../../../../releases/v1.2.6/). The examples on this page have not been updated to reflect this recent addition.

Create a sample table

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

Start a new shell  with `ysqlsh` and begin another transaction to insert some more rows.

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

## See also

[`SET`](../txn_set)
[Other YSQL Statements](..)
