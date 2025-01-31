---
title: CREATE INDEX statement [YSQL]
headerTitle: CREATE INDEX
linkTitle: CREATE INDEX
description: Use the CREATE INDEX statement to create an index on the specified columns of the specified table.
menu:
  v2.14:
    identifier: ddl_create_index
    parent: statements
type: docs
---

## Synopsis

Use the `CREATE INDEX` statement to create an index on the specified columns of the specified table. Indexes are primarily used to improve query performance.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-bs-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-bs-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_index,index_elem.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/create_index,index_elem.diagram.md" %}}
  </div>
</div>

## Semantics

When an index is created on a populated table, YugabyteDB automatically backfills the existing data into the index.
In most cases, this uses an online schema migration.
The following table explains some of the differences between creating an index online and not online.

| Condition | Online | Not online |
| :-------- | :----- | :--------- |
| Safe to do other DMLs during `CREATE INDEX`? | yes | no |
| Keeps other transactions alive during `CREATE INDEX`? | mostly | no |
| Parallelizes index loading? | yes | no |

`CREATE INDEX CONCURRENTLY` is supported, though online index backfill is enabled by default. Some restrictions apply (see [CONCURRENTLY](#concurrently)).

To disable online schema migration for YSQL `CREATE INDEX`, set the flag `ysql_disable_index_backfill=true` on **all** nodes and **both** master and tserver.

To disable online schema migration for one `CREATE INDEX`, use `CREATE INDEX NONCONCURRENTLY`.

{{< note title="Note" >}}

For details on how online index backfill works, refer to [Online Index Backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md).

{{< /note >}}

Regarding colocation, indexes follow their table. If the table is colocated, its index is also colocated; if the table is not colocated, its index is also not colocated.

### Partitioned Indexes

Creating an index on a partitioned table automatically creates a corresponding index for every partition in the default tablespace. It's also possible to create an index on each partition individually, which you should do in the following cases:

* Parallel writes are expected while creating the index, because concurrent builds for indexes on partitioned tables aren't supported. In this case, it's better to use concurrent builds to create indexes on each partition individually.
* [Row-level geo-partitioning](../../../../../explore/multi-region-deployments/row-level-geo-partitioning/) is being used. In this case, create the index separately on each partition to customize the tablespace in which each index is created.
* `CREATE INDEX CONCURRENTLY` is not supported for partitioned tables (see [CONCURRENTLY](#concurrently)).

### UNIQUE

Enforce that duplicate values in a table are not allowed.

### CONCURRENTLY
Enable online schema migration (see [Semantics](#semantics) for details), with some restrictions:
* When creating an index on a temporary table, online schema migration is disabled.
* `CREATE INDEX CONCURRENTLY` is not supported for partitioned tables.
* `CREATE INDEX CONCURRENTLY` is not supported inside a transaction block.

### NONCONCURRENTLY

Disable online schema migration (see [Semantics](#semantics) for details).

### ONLY

Indicates not to recurse creating indexes on partitions, if the table is partitioned. The default is to recurse.

### *access_method_name*

The name of the index access method.
By default, `lsm` is used for YugabyteDB tables and `btree` is used otherwise (for example, temporary tables).
[GIN indexes](../../../../../explore/indexes-constraints/gin/) can be created in YugabyteDB by using the `ybgin` access method.

### INCLUDE clause

Specify a list of columns which will be included in the index as non-key columns.

### TABLESPACE clause

Specify the name of the [tablespace](../../../../../explore/ysql-language-features/going-beyond-sql/tablespaces/) that describes the placement configuration for this index. By default, indexes are placed in the `pg_default` tablespace, which spreads the tablets of the index evenly across the cluster.

### WHERE clause

A [partial index](#partial-indexes) is an index that is built on a subset of a table and includes only rows that satisfy the condition specified in the `WHERE` clause.
It can be used to exclude NULL or common values from the index, or include just the rows of interest.
This will speed up any writes to the table since rows containing the common column values don't need to be indexed.
It will also reduce the size of the index, thereby improving the speed for read queries that use the index.

#### *name*

 Specify the name of the index to be created.

#### *table_name*

Specify the name of the table to be indexed.

### *index_elem*

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

To specify the number of tablets for an index, you can use the `CREATE INDEX` statement with the [`SPLIT INTO`](#split-into) clause.

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

## Troubleshooting

If the following troubleshooting tips don't resolve your issue, please ask for help in our [community Slack]({{<slack-invite>}}) or [file a GitHub issue](https://github.com/yugabyte/yugabyte-db/issues/new?title=Index+backfill+failure).

**If online `CREATE INDEX` fails**, it likely failed in the backfill step.
In that case, the index exists but is not usable.
Drop the index and try again.
If it still doesn't work, here are some troubleshooting steps:

- **Did it time out?** Try increasing timeout flags:
  - master `ysql_index_backfill_rpc_timeout_ms`
  - tserver `backfill_index_client_rpc_timeout_ms`
- **Did you get a "backfill failed to connect to DB" error?** You may be hitting an issue with authentication. If you're on a stable version prior to 2.4 or a latest (2.3.x or 2.5.x) version prior to 2.5.2, online `CREATE INDEX` does not work with authentication enabled.
  - For version 2.5.1, you can use `CREATE INDEX NONCONCURRENTLY` as a workaround.
  - If the version is at least 2.3, you can set `ysql_disable_index_backfill=false` as a workaround.
  - In all supported versions, you can disable authentication (for example, by using `ysql_enable_auth`, `ysql_hba_conf`, or `ysql_hba_conf_csv`) as a workaround.
- **Did you get a "duplicate key value" error?**
  Then, you have a unique constraint violation.

**To prioritize keeping other transactions alive** during the index backfill, bump up the following:

- master flag `index_backfill_wait_for_old_txns_ms`
- YSQL parameter `yb_index_state_flags_update_delay`

**To speed up index creation** by a few seconds when you know there will be no online writes, set the YSQL parameter `yb_index_state_flags_update_delay` to zero.
