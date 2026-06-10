---
title: Transactional DDL
headerTitle: Transactional DDL
linkTitle: Transactional DDL
description: Learn how YugabyteDB handles DDLs in a transaction
headcontent: Learn how YugabyteDB handles DDLs in a transaction
tags:
   feature: tech-preview
menu:
  stable:
    identifier: transactional-ddl
    parent: explore-transactions
    weight: 300
type: docs
---

Data Definition Language (DDL) statements modify the structure of your data. PostgreSQL allows executing such statements within a transactional block and supports rolling them back as part of a transaction rollback. For two transactions undergoing conflicting DDLs, it also provides isolation guarantees by taking appropriate object locks during DDL processing.

YugabyteDB's transactional DDL provides similar guarantees for rolling back DDL operations done inside a transaction block. However, because object locks are still under active development, isolation guarantees are weaker than in PostgreSQL. See [Limitations](#limitations) for more information.

## Enable transactional DDL

{{<tags/feature/tp idea="1677">}} Support for transactional DDL is disabled by default, and to enable the feature, set the [yb-tserver](../../../reference/configuration/yb-tserver/) flag `ysql_yb_ddl_transaction_block_enabled` to true.

Because `ysql_yb_ddl_transaction_block_enabled` is a preview flag, to use it, add the flag to the [allowed_preview_flags_csv](../../../reference/configuration/yb-tserver/#allowed-preview-flags-csv) list (that is, `allowed_preview_flags_csv=ysql_yb_ddl_transaction_block_enabled`).

## Rollback capabilities

All DDLs supported in YugabyteDB provide the same rollback capabilities as PostgreSQL. These include DDLs on tables, indexes, roles, and materialized views.

Note that some DDL statements such as DDLs on database or tablespaces are disallowed in a transaction block in PostgreSQL, and are also disallowed in YugabyteDB.

The following example demonstrates how DDL statements, such as ALTER TABLE, behave in a PostgreSQL-compatible transaction in YugabyteDB. It highlights the atomicity of transactions, where all changes (both DML and DDL) are either committed together or entirely rolled back.

```sql
yugabyte=# CREATE TABLE foo (bar int);
yugabyte=# INSERT INTO foo VALUES (1);
yugabyte=# BEGIN;
yugabyte=*# INSERT INTO foo VALUES (2);
yugabyte=*# ALTER TABLE foo ADD COLUMN name text;
yugabyte=*# INSERT INTO foo VALUES (3, 'test');
yugabyte=*# SELECT * FROM foo;
```

```output
 bar | name
-----+------
   1 |
   2 |
   3 | test
(3 rows)
```

```sql
yugabyte=*# ROLLBACK;
yugabyte=# SELECT * FROM foo;
```

```output
 bar
-----
   1
(1 row)
```

## Limitations

- [Concurrent DDLs](../../../best-practices-operations/administration/#concurrent-ddl-during-a-ddl-operation) on the same database are unsupported and will lead to conflict and read restart required errors. Your applications must handle these by retrying the statements.

- [Savepoints](/stable/develop/learn/transactions/transactions-retries-ysql/#savepoints) are unsupported for DDL statements. As a result, you cannot create a savepoint in a transaction block that has executed a DDL statement. Similarly, you cannot execute a DDL statement in a transaction block in which a savepoint has been created.

- Concurrent transaction blocks with DDLs that touch the same or different catalog data can result in undefined behavior without any error. This situation also applies to DDLs that are run concurrently without transactional DDL enabled. The difference is that, with transactional DDL enabled, it is easier for such behavior to occur when using READ COMMITTED isolation level. With transactional DDL in REPEATABLE READ isolation level, the chances of such an issue occurring are slim (similar to when transactional DDL is disabled).

  For example, in the following sequence, the column `a` added by Session 1 is missing after Session 2 commits:

    ```sql
    -- Session 1                                  -- Session 2

    BEGIN;
                                                   BEGIN;
    ALTER TABLE test ADD COLUMN a INT;
                                                   ALTER TABLE test ALTER COLUMN v TYPE SMALLINT;
                                                   -- waiting....
    COMMIT;
                                                   COMMIT;

    SELECT * FROM test;  -- Column "a" is missing
    ```

- When Auto Analyze runs with transactional DDL enabled, and a user DDL is executed concurrently in a transaction block (also with transactional DDL enabled), it can lead to a serialization error either on the user DDL or on the COMMIT of the transaction block containing the user DDL.

For an overview of common concepts used in YugabyteDB's implementation of distributed transactions, see [Distributed transactions](../distributed-transactions-ysql/).
