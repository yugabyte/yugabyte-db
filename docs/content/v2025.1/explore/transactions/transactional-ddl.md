---
title: Transactional DDL
headerTitle: Transactional DDL
linkTitle: Transactional DDL
description: Learn how YugabyteDB handles DDLs in a transaction
headcontent: Learn how YugabyteDB handles DDLs in a transaction
tags:
   feature: tech-preview
menu:
  v2025.1:
    identifier: transactional-ddl
    parent: explore-transactions
    weight: 300
type: docs
---

Data Definition Language (DDL) statements modify the structure of your data. PostgreSQL allows executing such statements within a transactional block and supports rolling them back as part of a transaction rollback. For two transactions undergoing conflicting DDLs, it also provides isolation guarantees by taking appropriate object locks during DDL processing.

YugabyteDB's transactional DDL provides similar guarantees for rolling back DDL operations done inside a transaction block. However, because object locks are still under active development, isolation guarantees are weaker than in PostgreSQL. See [Limitations](#limitations) for more information.

## Enable transactional DDL

To enable transactional DDL, set the [yb-tserver](../../../reference/configuration/yb-tserver/) flag `yb_ddl_transaction_block_enabled` to true.

Because `yb_ddl_transaction_block_enabled` is a preview flag, to use it, add the flag to the [allowed_preview_flags_csv](../../../reference/configuration/yb-tserver/#allowed-preview-flags-csv) list (that is, `allowed_preview_flags_csv=yb_ddl_transaction_block_enabled`).

### Rollback capabilities

All DDLs supported in YugabyteDB provide the same rollback capabilities as PostgreSQL. These include DDLs on tables, indexes, roles, and materialized views.

Note that some DDL statements such as DDLs on database or tablespaces are disallowed in a transaction block in PostgreSQL, and are also disallowed in YugabyteDB.

#### Rollback CREATE TABLE

This example demonstrates rolling back a `CREATE TABLE` statement within a transaction block.

```sql
yugabyte=# BEGIN;
yugabyte=*# CREATE TABLE foo (bar int);
yugabyte=*# INSERT INTO foo VALUES (1);
yugabyte=*# ROLLBACK;
yugabyte=# SELECT * FROM foo;
```

```output
ERROR:  relation "foo" does not exist
LINE 1: SELECT * FROM foo;
                      ^
```

#### Rollback ALTER TABLE

This example demonstrates rolling back an `ALTER TABLE` statement that adds a column.

```sql
yugabyte=# CREATE TABLE foo (bar int);
yugabyte=# BEGIN;
yugabyte=*# ALTER TABLE foo ADD COLUMN name text;
yugabyte=*# SELECT * FROM foo;
```

```output
 bar | name
-----+------
(0 rows)
```

```sql
yugabyte=*# ROLLBACK;
yugabyte=# SELECT * FROM foo;
 bar
-----
(0 rows)
```

#### Rollback Materialized View

This example demonstrates rolling back the creation of a materialized view.

```sql
yugabyte=# CREATE TABLE t4 (id int);
yugabyte=# INSERT INTO t4 VALUES (1);
yugabyte=# BEGIN;
yugabyte=*# CREATE MATERIALIZED VIEW mv4 AS SELECT * FROM t4;
yugabyte=*# ROLLBACK;
yugabyte=# SELECT * FROM mv4;
```

```output
ERROR:  relation "mv4" does not exist
LINE 1: SELECT * FROM mv4;
                      ^
```

#### Rollback ALTER ROLE

This example demonstrates rolling back an `ALTER ROLE` statement that grants a privilege.

```sql
yugabyte=# CREATE ROLE test_user;
yugabyte=#
yugabyte=# BEGIN;
yugabyte=*# ALTER ROLE test_user WITH CREATEDB;
yugabyte=*# \du test_user
```

```output
                  List of roles
 Role name |       Attributes        | Member of
-----------+-------------------------+-----------
 test_user | Create DB, Cannot login | {}
```

```sql
yugabyte=*# ROLLBACK;

yugabyte=# -- Role doesn't have CREATEDB privilege
yugabyte=# \du test_user
```

```output
            List of roles
 Role name |  Attributes  | Member of
-----------+--------------+-----------
 test_user | Cannot login | {}
```

### Limitations

- Concurrent DDLs on the same database are unsupported and will lead to conflict and read restart required errors. Your applications must handle these by retrying the statements.

- [Savepoints](../../develop/learn/transactions/transactions-retries-ysql/#savepoints) are unsupported for DDL statements. As a result, you cannot create a savepoint in a transaction block that has executed a DDL statement. Similarly, you cannot execute a DDL statement in a transaction block in which a savepoint has been created.

For an overview of common concepts used in YugabyteDB's implementation of distributed transactions, see [Distributed transactions](../distributed-txns/).
