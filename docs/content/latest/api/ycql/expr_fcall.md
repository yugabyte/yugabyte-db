---
title: Function Call
summary: Combination of one or more values.
description: Built-in Function Call
menu:
  latest:
    parent: api-cassandra
    weight: 1350
aliases:
  - /latest/api/cassandra/expr_fcall
  - /latest/api/ycql/expr_fcall
isTocNested: true
showAsideToc: true
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
| [DateOf](../function_datetime/#dateof) | [`TIMESTAMP`](../type_datetime) | ([`TIMEUUID`](../type_uuid)) | Conversion |
| MaxTimeUuid | [`TIMEUUID`](../type_uuid) | ([`TIMESTAMP`](../type_datetime)) | Returns the associated max time uuid  |
| MinTimeUuid | [`TIMEUUID`](../type_uuid) | ([`TIMESTAMP`](../type_datetime)) | Returns the associated min time uuid  |
| [CurrentDate](../function_datetime/#currentdate-currenttime-and-currenttimestamp) | [`DATE`](../type_datetime) | () | Return the system current date |
| [CurrentTime](../function_datetime/#currentdate-currenttime-and-currenttimestamp) | [`TIME`](../type_datetime) | () | Return the system current time of day |
| [CurrentTimestamp](../function_datetime/#currentdate-currenttime-and-currenttimestamp) | [`TIMESTAMP`](../type_datetime) | () | Return the system current timestamp |
| [Now](../function_datetime/#now) | [`TIMEUUID`](../type_uuid) | () | Returns the UUID of the current timestamp |
| [TTL](#ttl-function) | [`BIGINT`](../type_int) | (\<AnyType>) | Get time-to-live of a column |
| [ToDate](../function_datetime/#todate) | [`DATE`](../type_datetime) | ([`TIMESTAMP`](../type_datetime)) | Conversion |
| [ToDate](../function_datetime/#todate) | [`DATE`](../type_datetime) | ([`TIMEUUID`](../type_uuid)) | Conversion |
| ToTime | [`TIME`](../type_datetime) | ([`TIMESTAMP`](../type_datetime))  | Conversion |
| ToTime | [`TIME`](../type_datetime) | ([`TIMEUUID`](../type_uuid) | Conversion |
| [ToTimestamp](../function_datetime/#totimestamp) | ([`TIMESTAMP`](../type_datetime))  | ([`DATE`](../type_datetime)) | Conversion |
| [ToTimestamp](../function_datetime/#totimestamp) | ([`TIMESTAMP`](../type_datetime)) | (`TIMEUUID`) | Conversion |
| [ToUnixTimestamp](../function_datetime/#tounixtimestamp) | [`BIGINT`](../type_int) | ([`DATE`](../type_datetime)) | Conversion |
| [ToUnixTimestamp](../function_datetime/#tounixtimestamp) | [`BIGINT`](../type_int) | ([`TIMESTAMP`](../type_datetime))  | Conversion |
| [ToUnixTimestamp](../function_datetime/#tounixtimestamp) | [`BIGINT`](../type_int) | ([`TIMEUUID`](../type_uuid)) | Conversion |
| [UnixTimestampOf](../function_datetime/#unixtimestampof) | [`BIGINT`](../type_int) | ([`TIMEUUID`](../type_uuid)) | Conversion |
| [UUID](../function_datetime/#uuid) | [`UUID`](../type_uuid) | () | Returns a version 4 UUID |
| [WriteTime](#writetime-function) | [`BIGINT`](../type_int) | (\<AnyType>) | Returns the timestamp when the column was written |

## Aggregate Functions

| Function | Description |
|----------|-------------|
| COUNT | Returns number of selected rows |
| SUM | Returns sums of column values |
| AVG | Returns the average of column values |
| MIN | Returns the minimum value of column values |
| MAX | Returns the maximum value of column values |

## Semantics

<li>The argument data types must be convertible to the expected type for that argument that was specified by the function definition.</li>
<li>Function execution will return a value of the specified type by the function definition.</li>
<li>YugabyteDB allows function calls to be used any where that expression is allowed.</li>

## Cast function

```
cast_call ::= CAST '(' column AS type ')'
```

CAST function converts the value returned from a table column to the specified data type.

| Source Column Type | Target Data Type |
|--------------------|------------------|
| `BIGINT` | `SMALLINT`, `INT`, `TEXT` |
| `BOOLEAN` | `TEXT` |
| `DATE` | `TEXT`, `TIMESTAMP` |
| `DOUBLE` | `BIGINT`, `INT`, `SMALLINT`, `TEXT` |
| `FLOAT` | `BIGINT`, `INT`, `SMALLINT`, `TEXT` |
| `INT` | `BIGINT`, `SMALLINT`, `TEXT` |
| `SMALLINT` | `BIGINT`, `INT`, `TEXT` |
| `TIME` | `TEXT` |
| `TIMESTAMP` | `DATE`, `TEXT` |
| `TIMEUUID` | `DATE`, `TIMESTAMP` |

## WriteTime function

The `WriteTime` function returns the timestamp in microseconds when a column was written.
For example, suppose we have a table `page_views` with a column named `views`:

```sql
 SELECT writetime(views) FROM page_views;

 writetime(views)
------------------
 1572882871160113

(1 rows)
```

## TTL function

The TTL function returns the number of seconds until a column or row expires. 
Assuming we have a table `page_views` and a column named `views`:

```sql
SELECT TTL(views) FROM page_views;

 ttl(views)
------------
      86367

(1 rows)
```

## Examples

```sql
cqlsh:example> CREATE TABLE test_cast (k INT PRIMARY KEY, ts TIMESTAMP);
```

```sql
cqlsh:example> INSERT INTO test_cast (k, ts) VALUES (1, '2018-10-09 12:00:00');
```

```sql
cqlsh:example> SELECT CAST(ts AS DATE) FROM test_cast;
```

```
 cast(ts as date)
------------------
       2018-10-09
```

## See also

- [All Expressions](..##expressions)
