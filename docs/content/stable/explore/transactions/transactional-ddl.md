---
title: Transactional DDL
headerTitle: Transactional DDL
linkTitle: Transactional DDL
description: Learn how YugabyteDB handles DDLs in a transaction
headcontent: Learn how YugabyteDB handles DDLs in a transaction
tags:
   feature: early-access
menu:
  stable:
    identifier: transactional-ddl
    parent: explore-transactions
    weight: 300
type: docs
---

YugabyteDB can roll back DDL statements that run inside a transaction block. All DDLs supported in YugabyteDB provide the same rollback capabilities as PostgreSQL. These include DDLs on tables, indexes, roles, and materialized views.

For more information and current limitations, see [Transactional DDL](../../../architecture/transactions/transactional-ddl/).

## Roll back a DDL statement

{{% explore-setup-single-new %}}

While in early access, transactional DDL is disabled by default. To enable the feature, set the [yb-tserver](../../../reference/configuration/yb-tserver/) flag `ysql_yb_ddl_transaction_block_enabled` to true.

The following example shows an `ALTER TABLE` inside a transaction. The insert and the schema change commit together, or they both roll back.

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

To speed up bulk loads into tables created in the same transaction, see [Faster writes to new tables](../new-table-writes/). That optimization's transaction-block support depends on transactional DDL.
