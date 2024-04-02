---
title: CREATE INDEX statement [YSQL]
headerTitle: CREATE INDEX
linkTitle: CREATE INDEX
description: Use the CREATE INDEX statement to create an index on the specified columns of the specified table.
menu:
  preview:
    identifier: ddl_create_index
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE INDEX` statement to create an index on the specified columns of the specified table. Indexes are primarily used to improve query performance.

## Syntax

{{%ebnf%}}
  create_index,
  index_elem
{{%/ebnf%}}

## Semantics

When an index is created on a populated table, YugabyteDB automatically backfills the existing data into the index. In most cases, this uses an online schema migration. The following table explains some differences between creating an index online and not online.

| Condition | Online | Not online |
| :-------- | :----- | :--------- |
| Safe to do other DMLs during `CREATE INDEX`? | yes | no |
| Keeps other transactions alive during `CREATE INDEX`? | mostly | no |
| Parallelizes index loading? | yes | no |

`CREATE INDEX CONCURRENTLY` is supported, though online index backfill is enabled by default. Some restrictions apply (see [CONCURRENTLY](#concurrently)).

To disable online schema migration for YSQL `CREATE INDEX`, set the flag `ysql_disable_index_backfill=true` on **all** nodes and **both** YB-Master and YB-TServer.

To disable online schema migration for one `CREATE INDEX`, use `CREATE INDEX NONCONCURRENTLY`.

{{< note title="Note" >}}

For details on how online index backfill works, refer to [Online Index Backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md).

{{< /note >}}

Regarding colocation, indexes follow their table. If the table is colocated, its index is also colocated; if the table is not colocated, its index is also not colocated.

### Partitioned Indexes

Creating an index on a partitioned table automatically creates a corresponding index for every partition in the default tablespace. It's also possible to create an index on each partition individually, which you should do in the following cases:

- Parallel writes are expected while creating the index, because concurrent builds for indexes on partitioned tables aren't supported. In this case, it's better to use concurrent builds to create indexes on each partition individually.
- [Row-level geo-partitioning](../../../../../explore/multi-region-deployments/row-level-geo-partitioning/) is being used. In this case, create the index separately on each partition to customize the tablespace in which each index is created.
- `CREATE INDEX CONCURRENTLY` is not supported for partitioned tables (see [CONCURRENTLY](#concurrently)).

### UNIQUE

Enforce that duplicate values in a table are not allowed.

### CONCURRENTLY

Enable online schema migration (see [Semantics](#semantics) for details), with some restrictions:

- When creating an index on a temporary table, online schema migration is disabled.
- `CREATE INDEX CONCURRENTLY` is not supported for partitioned tables.
- `CREATE INDEX CONCURRENTLY` is not supported inside a transaction block.

### NONCONCURRENTLY

Disable online schema migration (see [Semantics](#semantics) for details).

### ONLY

Indicates not to recurse creating indexes on partitions, if the table is partitioned. The default is to recurse.

### *access_method_name*

The name of the index access method. By default, `lsm` is used for YugabyteDB tables and `btree` is used otherwise (for example, temporary tables).

[GIN indexes](../../../../../explore/indexes-constraints/gin/) can be created in YugabyteDB by using the `ybgin` access method.

### INCLUDE clause

Specify a list of columns which will be included in the index as non-key columns.

### TABLESPACE clause

Specify the name of the [tablespace](../../../../../explore/ysql-language-features/going-beyond-sql/tablespaces/) that describes the placement configuration for this index. By default, indexes are placed in the `pg_default` tablespace, which spreads the tablets of the index evenly across the cluster.

### WHERE clause

A [partial index](#partial-indexes) is an index that is built on a subset of a table and includes only rows that satisfy the condition specified in the `WHERE` clause.

It can be used to exclude NULL or common values from the index, or include just the rows of interest.

This speeds up any writes to the table because rows containing the common column values don't need to be indexed.

It also reduces the size of the index, thereby improving the speed for read queries that use the index.

#### *name*

Specify the name of the index to be created.

#### *table_name*

Specify the name of the table to be indexed.

#### *index_elem*

#### *column_name*

Specify the name of a column of the table.

#### *expression*

Specify one or more columns of the table and must be surrounded by parentheses.

- `HASH` - Use hash of the column. This is the default option for the first column and is used to shard the index table.
- `ASC` — Sort in ascending order. This is the default option for second and subsequent columns of the index.
- `DESC` — Sort in descending order.
- `NULLS FIRST` - Specifies that nulls sort before non-nulls. This is the default when DESC is specified.
- `NULLS LAST` - Specifies that nulls sort after non-nulls. This is the default when DESC is not specified.

### SPLIT INTO

For hash-sharded indexes, you can use the `SPLIT INTO` clause to specify the number of tablets to be created for the index. The hash range is then evenly split across those tablets.

Presplitting indexes, using `SPLIT INTO`, distributes index workloads on a production cluster. For example, if you have 3 servers, splitting the index into 30 tablets can provide higher write throughput on the index. For an example, see [Create an index specifying the number of tablets](#create-an-index-specifying-the-number-of-tablets).

{{< note title="Note" >}}

By default, YugabyteDB presplits an index into `ysql_num_shards_per_tserver * num_of_tserver` tablets. The `SPLIT INTO` clause can be used to override that setting on a per-index basis.

{{< /note >}}

### SPLIT AT VALUES

For range-sharded indexes, you can use the `SPLIT AT VALUES` clause to set split points to presplit range-sharded indexes.

**Example**

```plpgsql
CREATE TABLE tbl(
  a INT,
  b INT,
  PRIMARY KEY(a ASC, b DESC);
);

CREATE INDEX idx1 ON tbl(b ASC, a DESC) SPLIT AT VALUES((100), (200), (200, 5));
```

In the example above, there are three split points, so four tablets will be created for the index:

- tablet 1: `b=<lowest>, a=<lowest>` to `b=100, a=<lowest>`
- tablet 2: `b=100, a=<lowest>` to `b=200, a=<lowest>`
- tablet 3: `b=200, a=<lowest>` to `b=200, a=5`
- tablet 4: `b=200, a=5` to `b=<highest>, a=<highest>`

{{< note title="Note" >}}

By default, YugabyteDB creates a range sharded index as a single tablet. The `SPLIT AT` clause can be used to override that setting on a per-index basis.

{{< /note >}}

## Examples

### Unique index with HASH column ordering

Create a unique index with hash ordered columns.

```plpgsql
yugabyte=# CREATE TABLE products(id int PRIMARY KEY,
                                 name text,
                                 code text);
yugabyte=# CREATE UNIQUE INDEX ON products(code);
yugabyte=# \d products
              Table "public.products"
 Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
 id     | integer |           | not null |
 name   | text    |           |          |
 code   | text    |           |          |
Indexes:
    "products_pkey" PRIMARY KEY, lsm (id HASH)
    "products_code_idx" UNIQUE, lsm (code HASH)
```

### ASC ordered index

Create an index with ascending ordered key.

```plpgsql
yugabyte=# CREATE INDEX products_name ON products(name ASC);
yugabyte=# \d products_name
   Index "public.products_name"
 Column | Type | Key? | Definition
--------+------+------+------------
 name   | text | yes  | name
lsm, for table "public.products
```

### INCLUDE columns

Create an index with ascending ordered key and include other columns as non-key columns

```plpgsql
yugabyte=# CREATE INDEX products_name_code ON products(name) INCLUDE (code);
yugabyte=# \d products_name_code;
 Index "public.products_name_code"
 Column | Type | Key? | Definition
--------+------+------+------------
 name   | text | yes  | name
 code   | text | no   | code
lsm, for table "public.products"
```

### Create an index specifying the number of tablets

To specify the number of tablets for an index, you can use the `CREATE INDEX` statement with the [SPLIT INTO](#split-into) clause.

```plpgsql
CREATE TABLE employees (id int PRIMARY KEY, first_name TEXT, last_name TEXT) SPLIT INTO 10 TABLETS;
CREATE INDEX ON employees(first_name, last_name) SPLIT INTO 10 TABLETS;
```

### Partial indexes

Consider an application maintaining shipments information. It has a `shipments` table with a column for `delivery_status`. If the application needs to access in-flight shipments frequently, then it can use a partial index to exclude rows whose shipment status is `delivered`.

```plpgsql
yugabyte=# create table shipments(id int, delivery_status text, address text, delivery_date date);
yugabyte=# create index shipment_delivery on shipments(delivery_status, address, delivery_date) where delivery_status != 'delivered';
```

### Expression indexes

An index column need not be just a column of the underlying table, but can be a function, or scalar expression computed from one or more columns of the table. You can also obtain fast access to tables based on the results of computations.

A basic example is indexing unique emails in a users table similar to the following:

```plpgsql
CREATE TABLE users(id BIGSERIAL PRIMARY KEY, email TEXT NOT NULL);

CREATE UNIQUE INDEX users_email_idx ON users(lower(email));
```

Creating a unique index prevents inserting duplicate email addresses using a different case.

Note that index expressions are only evaluated at index time, so to use the index for a specific query the expression must match exactly. 

```plpgsql
SELECT * FROM users WHERE lower(email)='user@example.com'; # will use the index created above
SELECT * FROM users WHERE email='user@example.com'; # will NOT use the index
```

Expression indexes are often used to [index jsonb columns](../../../datatypes/type_json/create-indexes-check-constraints/).

## Troubleshooting

If the following troubleshooting tips don't resolve your issue, ask for help in our [community Slack]({{<slack-invite>}}) or [file a GitHub issue](https://github.com/yugabyte/yugabyte-db/issues/new?title=Index+backfill+failure).

### Invalid index

If online `CREATE INDEX` fails, an invalid index may be left behind. These indexes are not usable in queries and cause internal operations, so they should be dropped.

For example, the following commands can create an invalid index:

```plpgsql
yugabyte=# CREATE TABLE uniqueerror (i int);
```

```output
CREATE TABLE
```

```plpgsql
yugabyte=# INSERT INTO uniqueerror VALUES (1), (1);
```

```output
INSERT 0 2
```

```plpgsql
yugabyte=# CREATE UNIQUE INDEX ON uniqueerror (i);
```

```output
ERROR:  ERROR:  duplicate key value violates unique constraint "uniqueerror_i_idx"
```

```plpgsql
yugabyte=# \d uniqueerror
            Table "public.uniqueerror"
 Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
 i      | integer |           |          |
Indexes:
    "uniqueerror_i_idx" UNIQUE, lsm (i HASH) INVALID
```

Drop the invalid index as follows:

```plpgsql
yugabyte=# DROP INDEX uniqueerror_i_idx;
```

```output
DROP INDEX
```

### Common errors and solutions

- `ERROR:  duplicate key value violates unique constraint "uniqueerror_i_idx"`

  **Reason**: When creating a [unique index](#unique), a unique constraint violation was found.

  **Fix**: Resolve the conflicting row(s).

- `ERROR:  Backfilling indexes { timeoutmaster_i_idx } for tablet 42e3857759f54733a47e3bb817636f60 from key '' in state kFailed`

  **Reason**: Server-side backfill timeout is repeatedly hit.

  **Fixes**

  Do any or all of the following:

  - Increase the YB-Master flag [ysql_index_backfill_rpc_timeout_ms](../../../../../reference/configuration/yb-master/#ysql-index-backfill-rpc-timeout-ms) from 60000 (one minute) to 300000 (five minutes).
  - Increase the YB-TServer flag [backfill_index_timeout_grace_margin_ms](../../../../../reference/configuration/yb-tserver/#backfill-index-timeout-grace-margin-ms) from -1 (the system automatically calculates the value to be approximately 1 second) to 60000 (one minute).
  - Decrease the YB-TServer flag [backfill_index_write_batch_size](../../../../../reference/configuration/yb-tserver/#backfill-index-write-batch-size) from 128 to 32.

- `ERROR:  BackfillIndex RPC (request call id 123) to 127.0.0.1:9100 timed out after 86400.000s`

  **Reason**: Client-side backfill timeout is hit.

  **Fixes**

  The master leader may have changed during backfill. This is currently [not supported][backfill-master-failover-issue]. Retry creating the index, and keep an eye on the master leader.

  Try increasing parallelism. Index backfill happens in parallel across each tablet of the table. A one-tablet table in an [RF-3][rf] setup would not take advantage of the parallelism. (One-tablet tables are default for range-partitioned tables and colocated tables.) On the other hand, no matter how much parallelism there is, a one-tablet index would be a bottleneck for index backfill writes. Partitioning could be improved with [tablet splitting][tablet-splitting].

  In case the backfill really needs more time, increase [YB-TServer flag][yb-tserver] `backfill_index_client_rpc_timeout_ms` to as long as you expect the backfill to take (for example, one week).

**To prioritize keeping other transactions alive** during the index backfill, set each of the following to be longer than the longest transaction anticipated:

- [YB-Master flag][yb-master] `index_backfill_wait_for_old_txns_ms`
- YSQL parameter `yb_index_state_flags_update_delay`

**To speed up index creation** by a few seconds when you know there will be no online writes, set the YSQL parameter `yb_index_state_flags_update_delay` to zero.

[backfill-master-failover-issue]: https://github.com/yugabyte/yugabyte-db/issues/6218
[rf]: ../../../../../architecture/docdb-replication/replication/#replication-factor
[tablet-splitting]: ../../../../../architecture/docdb-sharding/tablet-splitting
[yb-master]: ../../../../../reference/configuration/yb-master/
[yb-tserver]: ../../../../../reference/configuration/yb-tserver/
