---
title: Explore Transactional DDL
headerTitle: Transactional DDL
linkTitle: Transactional DDL
description: Learn how to roll back DDL statements in a YugabyteDB transaction using transactional DDL.
headcontent: Learn how to roll back DDL statements in a transaction
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

{{< note title="Before you start" >}}

{{<tags/feature/ea idea="1677">}}This example requires transactional DDL (disabled by default). Create and connect to a YugabyteDB universe, then enable `ysql_yb_ddl_transaction_block_enabled` as described in [Enable transactional DDL](../../../architecture/transactions/transactional-ddl/#enable-transactional-ddl).

{{< /note >}}

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
