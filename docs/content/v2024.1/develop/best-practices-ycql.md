---
title: Best practices for YCQL applications
headerTitle: Best practices
linkTitle: Best practices
description: Tips and tricks to build YCQL applications
headcontent: Tips and tricks to build YCQL applications for high performance and availability
menu:
  v2024.1:
    identifier: best-practices-ycql
    parent: develop
    weight: 571
type: docs
---

{{<api-tabs>}}

## Global secondary indexes

Indexes use multi-shard transactional capability of YugabyteDB and are global and strongly consistent (ACID). To add secondary indexes, you need to create tables with [transactions enabled](../../api/ycql/ddl_create_table/#table-properties-1). They can also be used as materialized views by using the [`INCLUDE` clause](../../api/ycql/ddl_create_index#included-columns).

## Unique indexes

YCQL supports [unique indexes](../../api/ycql/ddl_create_index#unique-index). A unique index disallows duplicate values from being inserted into the indexed columns.

## Covering indexes

When querying by a secondary index, the original table is consulted to get the columns that aren't specified in the index. This can result in multiple random reads across the main table.

Sometimes, a better way is to include the other columns that you're querying that are not part of the index using the [`INCLUDE` clause](../../api/ycql/ddl_create_index/#included-columns). When additional columns are included in the index, they can be used to respond to queries directly from the index without querying the table.

This turns a (possible) random read from the main table to just a filter on the index.

## Atomic read modify write operations with UPDATE IF EXISTS

For operations like `UPDATE ... IF EXISTS` and `INSERT ... IF NOT EXISTS` that require an atomic read-modify-write, Apache Cassandra uses LWT which requires 4 round-trips between peers. These operations are supported in YugabyteDB a lot more efficiently, because of YugabyteDB's CP (in the CAP theorem) design based on strong consistency, and require only a single Raft-round trip between peers. Number and counter types work the same and don't need a separate "counters" table.

## JSONB

YugabyteDB supports the [`jsonb`](../../api/ycql/type_jsonb/) data type to model JSON data, which does not have a set schema and might change often. You can use JSONB to group less accessed columns of a table. YCQL also supports JSONB expression indexes that can be used to speed up data retrieval that would otherwise require scanning the JSON entries.

{{< note title="Use JSONB columns only when necessary" >}}

`jsonb` columns are slower to read and write compared to normal columns. They also take more space because they need to store keys in strings and make keeping data consistency more difficult. A good schema design is to keep most columns as regular columns or collections, and use `jsonb` only for truly dynamic values. Don't create a `data jsonb` column where you store everything; instead, use a `dynamic_data jsonb` column with the others being primitive columns.

{{< /note >}}

## Increment and decrement numeric types

In YugabyteDB, YCQL extends Apache Cassandra to add increment and decrement operators for integer data types. [Integers](../../api/ycql/type_int) can be set, inserted, incremented, and decremented while `COUNTER` can only be incremented or decremented. YugabyteDB implements CAS(compare-and-set) operations in one round trip, compared to four for Apache Cassandra.

## Expire older records automatically with TTL

YCQL supports automatic expiration of data using the [TTL feature](../../api/ycql/ddl_create_table/#use-table-property-to-define-the-default-expiration-time-for-rows). You can set a retention policy for data at table/row/column level and the older data is automatically purged from the database.

If configuring TTL for a time series dataset or any dataset with a table-level TTL, it is recommended for CPU and space efficiency to expire older files directly by using TTL-specific configuration options. More details can be found in [Efficient data expiration for TTL](../learn/ttl-data-expiration-ycql/#efficient-data-expiration-for-ttl).

{{<note title="Note">}}
TTL does not apply to transactional tables and so, its unsupported in that context.
{{</note>}}

## Use YugabyteDB drivers

Use YugabyteDB-specific [client drivers](../../drivers-orms/) because they are cluster- and partition-aware, and support `jsonb` columns.

## Leverage connection pooling in the YCQL client

A single client (for example, a multi-threaded application) should ideally use a single cluster object. The single cluster object typically holds underneath the covers a configurable number of connections to YB-TServers. Typically 1 or 2 connections per YB-TServer suffices to serve even 64-128 application threads. The same connection can be used for multiple outstanding requests, also known as multiplexing.

See also [Connection pooling](https://docs.datastax.com/en/developer/java-driver/4.6/manual/core/pooling/) in the DataStax Java Driver documentation.

## Use prepared statements

Whenever possible, use prepared statements to ensure that YugabyteDB partition-aware drivers can route queries to the tablet leader, to improve throughput, and eliminate the need for a server to parse the query on each operation.

## Use batching for higher throughput

Use batching for writing a set of operations to send all operations in a single RPC call instead of using multiple RPC calls, one per operation. Each batch operation has higher latency compared to single-row operations but has higher throughput overall.

## Column and row sizes

For consistent latency/performance, keep columns in the 2 MB range or less.

Big columns add up when selecting multiple columns or full rows. For consistent latency and performance, keep the size of individual rows in the 32 MB range or less.

## Don't use big collections

Collections are designed for storing small sets of values that are not expected to grow to arbitrary size (such as phone numbers or addresses for a user rather than posts or messages). While collections of larger sizes are allowed, they may have a significant impact on performance for queries involving them. In particular, some list operations (insert at an index and remove elements) require a read-before-write.

## Collections with many elements

Each element inside a collection ends up as a [separate key value](../../architecture/docdb/data-model#examples) in DocDB adding per-element overhead.

If your collections are immutable, or you update the whole collection in full, consider using the `JSONB` data type. An alternative would also be to use ProtoBuf or FlatBuffers and store the serialized data in a `BLOB` column.

## Use partition_hash for large table scans

`partition_hash` function can be used for querying a subset of the data to get approximate row counts or to break down full-table operations into smaller sub-tasks that can be run in parallel. See [example usage](../../api/ycql/expr_fcall#partition-hash-function) along with a working Python script.

## TRUNCATE tables instead of DELETE

[TRUNCATE](../../api/ycql/dml_truncate/) deletes the database files that store the table and is much faster than [DELETE](../../api/ycql/dml_delete/) which inserts a _delete marker_ for each row in transactions and they are removed from storage when a compaction runs.
