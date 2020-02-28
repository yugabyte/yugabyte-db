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
      General
    </a>
  </li>
  <li >
    <a href="{{< ref "best-practices-ycql.md" >}}" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
  <li >
    <a href="{{< ref "best-practices-ysql.md" >}}" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>


## JSONB datatype
YugabyteDB has [`jsonb`](https://docs.yugabyte.com/latest/api/ycql/type_jsonb/) datatype which is similar to 
Postgresql [`jsonb`](https://www.postgresql.org/docs/current/datatype-json.html) datatype. It is stored on disk in
binary format making searching & retrieval faster.

## Consistent & global Secondary indexes
Indexes use multi-shard transactional capability of YugabyteDB and are global and strongly consistent (ACID). 
To add secondary indexes you need to create tables with transactions enabled. 
They can also be used as materialized views by using the `INCLUDE` [clause](../../api/ycql/ddl_create_index#included-columns).

## Unique indexes
YCQL supports [unique indexes](../../api/ycql/ddl_create_index#unique-index). 
A unique index disallows duplicate values from being inserted into the indexed columns.

## UPDATE IF EXISTS
Operations like `UPDATE ... IF EXISTS`, `INSERT ... IF NOT EXISTS` which require an atomic read-modify-write, 
Apache Cassandra uses LWT which requires 4 round-trips between peers. These operations are supported in YugabyteDB a 
lot more efficiently, because of YugabyteDB's CP (in the CAP theorem) design based on strong consistency, 
and require only 1 Raft-round trip between peers. Number & Counter types work the same and don't need a separate "counters" table.

