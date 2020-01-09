---
title: 9. TTL for data expiry
linkTitle: 9. TTL for data expiry
description: TTL for data expiry
aliases:
  - /develop/learn/ttl-data-expiry/
menu:
  latest:
    identifier: ttl-data-expiry
    parent: learn
    weight: 581
showAsideToc: true
isTocNested: true
---
In YCQL there are two types of TTL, the table TTL and column level TTL. The column TTLs are stored
with the value of the column. The table TTL is not stored in DocDB (it is stored
in master’s syscatalog as part of the table’s schema). If no TTL is present at the column’s value,
the table TTL acts as the default value.

Furthermore, YCQL has a distinction between rows created using Insert vs Update. We keep track of
this difference (and row level TTLs) using a "liveness column", a special system column invisible to
the user. It is added for inserts, but not updates: making sure the row is present even if all
non-primary key columns are deleted only in the case of inserts.

 
## Table level TTL

**Table Level TTL**: YCQL allows the TTL property to be specified at the table level. 
In this case, we do not store the TTL on a per KV basis in DocDB; but the TTL is implicitly enforced 
on reads as well as during compactions (to reclaim space).
Table level TTL can be defined with `default_time_to_live` [property](../../../api/ycql/ddl_create_table#table-properties-1). 

Below, we will look at how the row-level TTL is achieved in detail.

## Row level & Column level TTL

YCQL allows the TTL property to be specified at  the level of each INSERT/UPDATE operation. 
In such cases, the TTL is stored as part of the RocksDB value. 

There are several ways to work with TTL:

1. Table level TTL with [`default_time_to_live`](../../../api/ycql/ddl_create_table#table-properties-1) property. 
2. [Expiring rows with TTL](../../../api/ycql/dml_insert#insert-a-row-with-expiration-time-using-the-using-ttl-clause)
3. [`TTL` function](../../../api/ycql/expr_fcall/#ttl-function) to return number of seconds until expiration
4. [`WriteTime` function](../../../api/ycql/expr_fcall#writetime-function) returns timestamp when row/column was inserted

