---
title: Best practices
linkTitle: Best practices
description: Best practices when using YugabyteDB
aliases:
  - /latest/quick-start/best-practices-ycql/
isTocNested: 4
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="{{< ref "best-practices.md" >}}" class="nav-link">
      <i class="icon-" aria-hidden="true"></i>
      DocDB
    </a>
  </li>
  <li >
    <a href="{{< ref "best-practices-ysql.md" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="{{< ref "best-practices-ycql.md" >}}" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>


## JSONB datatype
YugabyteDB has [`jsonb`](https://docs.yugabyte.com/latest/api/ycql/type_jsonb/) datatype that makes it easy to model 
json data which does not have a set schema and might change often. 
You can use jsonb to group less interesting / lesser accessed columns of a table. 
YCQL also supports JSONB expression indexes that can be used to speed up data retrieval that would otherwise require scanning the json entries.

### Use jsonb columns only when necessary
`jsonb` columns are slower to read/write compared to normal columns. 
They also take more space because they need to store keys in strings and make keeping data consistency harder.
A good schema design is to keep most columns as regular ones or collections, and only using `jsonb` for truly dynamic values. 
Don't create a `data jsonb` column where you put everything, but a `dynamic_data jsonb` column and other ones being 
primitive columns.

## Consistent & global Secondary indexes
Indexes use multi-shard transactional capability of YugabyteDB and are global and strongly consistent (ACID). 
To add secondary indexes you need to create tables with [transactions enabled](../api/ycql/ddl_create_table.md#table-properties-1). 
They can also be used as materialized views by using the `INCLUDE` [clause](../../api/ycql/ddl_create_index#included-columns).

## Unique indexes
YCQL supports [unique indexes](../../api/ycql/ddl_create_index#unique-index). 
A unique index disallows duplicate values from being inserted into the indexed columns.

## UPDATE IF EXISTS
Operations like `UPDATE ... IF EXISTS`, `INSERT ... IF NOT EXISTS` which require an atomic read-modify-write, 
Apache Cassandra uses LWT which requires 4 round-trips between peers. These operations are supported in YugabyteDB a 
lot more efficiently, because of YugabyteDB's CP (in the CAP theorem) design based on strong consistency, 
and require only 1 Raft-round trip between peers. Number & Counter types work the same and don't need a separate "counters" table.

## Use `TRUNCATE` to empty tables instead of `DELETE`
`TRUNCATE` deletes the database files that store the table and is very fast. 
While DELETE inserts a `delete marker` for each row  in transactions and they are removed from storage when a compaction 
runs.

## Covering indexes
When querying by a secondary index, the original table is consulted to get the columns that aren't specified in the 
index. This can result in multiple random reads across the main table.

Sometimes a better way is to include the other columns that we're querying that are not part of the index 
using the [`INCLUDE`](../api/ycql/ddl_create_index.md#included-columns) clause.  
When additional columns are included in the index, they can be used to respond to queries directly from the index without querying the table.

This turns a (possible) random read from the main table to just a filter on the index.


## Column size limit
For consistent latency/performance, we suggest keeping columns in the `2MB` range 
or less even though we support an individual column being about `32MB`.

## Row size limit
Big columns add up when selecting full rows or multiple of them. 
For consistent latency/performance, we suggest keeping the size in the `32MB` range
or less. This is a combination of [column sizing recommendations](#column-size-limit) for all columns.
