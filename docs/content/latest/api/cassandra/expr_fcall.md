---
title: Function Call
summary: Combination of one or more values.
description: Built-in Function Call
menu:
  latest:
    parent: api-cassandra
    weight: 1350
aliases:
  - api/cassandra/expr_fcall
  - api/cql/expr_fcall
  - api/ycql/expr_fcall
---

## Synopsis
Function call expression applies the specified function to to given arguments between parentheses and return the result of the computation.

## Syntax
```
function_call ::= function_name '(' [ arguments ... ] ')'
```

## Builtin Functions

| Function | Return Type | Argument Type | Description |
|----------|-------------|---------------|-------------|
| BlobAs\<Type> | \<Type> | ([`BLOB`](../type_blob)) | Converts a value from `BLOB` |
| \<Type>AsBlob | [`BLOB`](../type_blob) | (\<Type>) | Converts a value to `BLOB` |
| DateOf | [`TIMESTAMP`](../type_timestamp) | ([`TIMEUUID`](../type_uuid)) | Conversion |
| MaxTimeUuid | [`TIMEUUID`](../type_uuid) | ([`TIMESTAMP`](../type_timestamp)) | Returns the associated max time uuid  |
| MinTimeUuid | [`TIMEUUID`](../type_uuid) | ([`TIMESTAMP`](../type_timestamp)) | Returns the associated min time uuid  |
| Now | [`TIMEUUID`](../type_uuid) | () | Returns the UUID of the current timestamp |
| TTL | [`BIGINT`](../type_int) | (<AnyType>) | Seek time-to-live of a column |
| ToDate | `DATE` | ([`TIMESTAMP`](../type_timestamp)) | Conversion |
| ToDate | `DATE` | ([`TIMEUUID`](../type_uuid)) | Converts `TIMEUUID` to `DATE` |
| ToTime | `TIME` | ([`TIMESTAMP`](../type_timestamp))  | Conversion |
| ToTime | `TIME` | ([`TIMEUUID`](../type_uuid) | Conversion |
| [ToTimestamp](../function_datetime/#totimestamp) | ([`TIMESTAMP`](../type_timestamp))  | (`DATE`) | Conversion |
| [ToTimestamp](../function_datetime/#totimestamp) | ([`TIMESTAMP`](../type_timestamp)) | (`TIMEUUID`) | Conversion |
| [ToUnixTimestamp](../function_datetime/#tounixtimestamp) | [`BIGINT`](../type_int) | (`DATE`) | Conversion |
| [ToUnixTimestamp](../function_datetime/#tounixtimestamp) | [`BIGINT`](../type_int) | ([`TIMESTAMP`](../type_timestamp))  | Conversion |
| [ToUnixTimestamp](../function_datetime/#tounixtimestamp) | [`BIGINT`](../type_int) | ([`TIMEUUID`](../type_uuid)) | Conversion |
| [UnixTimestampOf](../function_datetime/#unixtimestampof) | [`BIGINT`](../type_int) | ([`TIMEUUID`](../type_uuid)) | Conversion |
| WriteTime | [`BIGINT`](../type_int) | (<AnyType>) | Returns the time when the column was written |

## Aggregate Functions

| Function | Description |
|----------|-------------|
| COUNT | Returns number of selected rows |
| SUM | Returns sums of column values |
| AVG | Returns the average of column values |
| MIN | Returns the minimum value of column values |
| MAX | Returns the maximum value of column values |

## Semantics

<li>The argument datatypes must be convertible to the expected type for that argument that was specified by the function definition.</li>
<li>Function execution will return a value of the specified type by the function definition.</li>
<li>YugaByte allows function calls to be used any where that expression is allowed.</li>

## See Also
[All Expressions](..##expressions)
