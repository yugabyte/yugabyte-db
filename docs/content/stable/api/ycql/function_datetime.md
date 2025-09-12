---
title: Date and time functions [YCQL]
headerTitle: Date and time functions
linkTitle: Date and time
description: Use date and time functions to work on date and time data types.
menu:
  preview_api:
    parent: api-cassandra
    weight: 1560
aliases:
  - /preview/api/cassandra/function_datetime
  - /preview/api/ycql/function_datetime
type: docs
---

This section covers the set of YCQL built-in functions that work on the date and time data types: [`DATE`, `TIME`, `TIMESTAMP`](../type_datetime/), or [`TIMEUUID`](../type_uuid).

## currentdate(), currenttime(), and currenttimestamp()

Use these functions to return the current system date and time in UTC time zone.

- They take no arguments.
- The return value is a `DATE`, `TIME`, or `TIMESTAMP`, respectively.

### Examples

#### Insert values using currentdate(), currenttime(), and currenttimestamp()

```sql
ycqlsh:example> CREATE TABLE test_current (k INT PRIMARY KEY, d DATE, t TIME, ts TIMESTAMP);
```

```sql
ycqlsh:example> INSERT INTO test_current (k, d, t, ts) VALUES (1, currentdate(), currenttime(), currenttimestamp());
```

#### Comparison using currentdate() and currenttime()

```sql
ycqlsh:example> SELECT * FROM test_current WHERE d = currentdate() and t < currenttime();
```

```output
 k | d          | t                  | ts
---+------------+--------------------+---------------------------------
 1 | 2018-10-09 | 18:00:41.688216000 | 2018-10-09 18:00:41.688000+0000
```

## now()

This function generates a new unique version 1 UUID (`TIMEUUID`).

- It takes in no arguments.
- The return value is a `TIMEUUID`.

### Examples

#### Insert values using now()

```sql
ycqlsh:example> CREATE TABLE test_now (k INT PRIMARY KEY, v TIMEUUID);
```

```sql
ycqlsh:example> INSERT INTO test_now (k, v) VALUES (1, now());
```

#### Select using now()

```sql
ycqlsh:example> SELECT now() FROM test_now;
```

```output
 now()
---------------------------------------
 b75bfaf6-4fe9-11e8-8839-6336e659252a
```

#### Comparison using now()

```sql
ycqlsh:example> SELECT v FROM test_now WHERE v < now();
```

```output
 v
---------------------------------------
 71bb5104-4fe9-11e8-8839-6336e659252a
```

## todate()

This function converts a timestamp or TIMEUUID to the corresponding date.

- It takes in an argument of type `TIMESTAMP` or `TIMEUUID`.
- The return value is a `DATE`.

```sql
ycqlsh:example> CREATE TABLE test_todate (k INT PRIMARY KEY, ts TIMESTAMP);
```

```sql
ycqlsh:example> INSERT INTO test_todate (k, ts) VALUES (1, currenttimestamp());
```

```sql
ycqlsh:example> SELECT todate(ts) FROM test_todate;
```

```output
 todate(ts)
------------
 2018-10-09
```

## minTimeUUID(<timestamp>)

This function generates corresponding (`TIMEUUID`) with minimum node/clock component so that it includes all regular
`TIMEUUID` with that timestamp when comparing with another `TIMEUUID`.

- It takes in an argument of type `TIMESTAMP`.
- The return value is a `TIMEUUID`.

### Examples

#### Insert values using now()

```sql
ycqlsh:example> CREATE TABLE test_min (k INT PRIMARY KEY, v TIMEUUID);
```

```sql
ycqlsh:example> INSERT INTO test_min (k, v) VALUES (1, now());
```

```sql
ycqlsh:ybdemo> select k, v, totimestamp(v) from test_min;
```

```output
 k | v                                    | totimestamp(v)
---+--------------------------------------+---------------------------------
 1 | dc79344c-cb79-11ec-915e-5219fa422f77 | 2022-05-04 07:14:39.205000+0000

(1 rows)
```

#### Select using minTimeUUID()

```sql
ycqlsh:ybdemo> SELECT * FROM test_min WHERE v > minTimeUUID('2022-04-04 13:42:00+0000');
```

```output
 k | v
---+--------------------------------------
 1 | dc79344c-cb79-11ec-915e-5219fa422f77

(1 rows)
```

## maxTimeUUID(<timestamp>)

This function generates corresponding (`TIMEUUID`) with maximum clock component so that it includes all regular
`TIMEUUID` with that timestamp when comparing with another `TIMEUUID`.

- It takes in an argument of type `TIMESTAMP`.
- The return value is a `TIMEUUID`.

### Examples

#### Insert values using now()

```sql
ycqlsh:example> CREATE TABLE test_max (k INT PRIMARY KEY, v TIMEUUID);
```

```sql
ycqlsh:example> INSERT INTO test_max (k, v) VALUES (1, now());
```

```sql
ycqlsh:ybdemo> SELECT k, v, totimestamp(v) from test_max;
```

```output
 k | v                                    | totimestamp(v)
---+--------------------------------------+---------------------------------
 1 | e9261bcc-395a-11eb-9edc-112a0241eb23 | 2020-12-08 13:40:18.636000+0000

(1 rows)
```

#### Select using maxTimeUUID()

```sql
ycqlsh:ybdemo> SELECT * FROM test_max WHERE v <= maxTimeUUID('2022-05-05 00:34:32+0000');
```

```output
 k | v
---+--------------------------------------
 1 | dc79344c-cb79-11ec-915e-5219fa422f77

(1 rows)
```

## totimestamp()

This function converts a date or TIMEUUID to the corresponding timestamp.

- It takes in an argument of type `DATE` or `TIMEUUID`.
- The return value is a `TIMESTAMP`.

### Examples

#### Insert values using totimestamp()

```sql
ycqlsh:example> CREATE TABLE test_totimestamp (k INT PRIMARY KEY, v TIMESTAMP);
```

```sql
ycqlsh:example> INSERT INTO test_totimestamp (k, v) VALUES (1, totimestamp(now()));
```

#### Select using totimestamp()

```sql
ycqlsh:example> SELECT totimestamp(now()) FROM test_totimestamp;
```

```output
 totimestamp(now())
---------------------------------
 2018-05-04 22:32:56.966000+0000
```

#### Comparison using totimestamp()

```sql
ycqlsh:example> SELECT v FROM test_totimestamp WHERE v < totimestamp(now());
```

```output
 v
---------------------------------
 2018-05-04 22:32:46.199000+0000
```

## dateof()

This function converts a TIMEUUID to the corresponding timestamp.

- It takes in an argument of type `TIMEUUID`.
- The return value is a `TIMESTAMP`.

### Examples

#### Insert values using dateof()

```sql
ycqlsh:example> CREATE TABLE test_dateof (k INT PRIMARY KEY, v TIMESTAMP);
```

```sql
ycqlsh:example> INSERT INTO test_dateof (k, v) VALUES (1, dateof(now()));
```

#### Select using dateof()

```sql
ycqlsh:example> SELECT dateof(now()) FROM test_dateof;
```

```output
 dateof(now())
---------------------------------
 2018-05-04 22:43:28.440000+0000
```

#### Comparison using dateof()

```sql
ycqlsh:example> SELECT v FROM test_dateof WHERE v < dateof(now());
```

```output
 v
---------------------------------
 2018-05-04 22:43:18.626000+0000
```

## tounixtimestamp()

This function converts TIMEUUID, date, or timestamp to a UNIX timestamp (which is
equal to the number of millisecond since epoch Thursday, 1 January 1970).

- It takes in an argument of type `TIMEUUID`, `DATE` or `TIMESTAMP`.
- The return value is a `BIGINT`.

### Examples

#### Insert values using tounixtimestamp()

```sql
ycqlsh:example> CREATE TABLE test_tounixtimestamp (k INT PRIMARY KEY, v BIGINT);
```

```sql
ycqlsh:example> INSERT INTO test_tounixtimestamp (k, v) VALUES (1, tounixtimestamp(now()));
```

#### Select using tounixtimestamp()

```sql
ycqlsh:example> SELECT tounixtimestamp(now()) FROM test_tounixtimestamp;
```

```output
 tounixtimestamp(now())
------------------------
          1525473993436
```

#### Comparison using tounixtimestamp()

You can do this as follows:

```sql
ycqlsh:example> SELECT v from test_tounixtimestamp WHERE v < tounixtimestamp(now());
```

```output
 v
---------------
 1525473942979
```

## unixtimestampof()

This function converts TIMEUUID or timestamp to a unix timestamp (which is
equal to the number of millisecond since epoch Thursday, 1 January 1970).

- It takes in an argument of type `TIMEUUID` or type `TIMESTAMP`.
- The return value is a `BIGINT`.

### Examples

#### Insert values using unixtimestampof()

```sql
ycqlsh:example> CREATE TABLE test_unixtimestampof (k INT PRIMARY KEY, v BIGINT);
```

```sql
ycqlsh:example> INSERT INTO test_unixtimestampof (k, v) VALUES (1, unixtimestampof(now()));
```

#### Select using unixtimestampof()

```sql
ycqlsh:example> SELECT unixtimestampof(now()) FROM test_unixtimestampof;
```

```output
 unixtimestampof(now())
------------------------
          1525474361676
```

#### Comparison using unixtimestampof()

```sql
ycqlsh:example> SELECT v from test_unixtimestampof WHERE v < unixtimestampof(now());
```

```output
 v
---------------
 1525474356781
```

## uuid()

This function generates a new unique version 4 UUID (`UUID`).

- It takes in no arguments.
- The return value is a `UUID`.

### Examples

#### Insert values using uuid()

```sql
ycqlsh:example> CREATE TABLE test_uuid (k INT PRIMARY KEY, v UUID);
```

```sql
ycqlsh:example> INSERT INTO test_uuid (k, v) VALUES (1, uuid());
```

#### Selecting the inserted uuid value

```sql
ycqlsh:example> SELECT v FROM test_uuid WHERE k = 1;
```

```output
 v
---------------------------------------
 71bb5104-4fe9-11e8-8839-6336e659252a
```

#### Select using uuid()

```sql
ycqlsh:example> SELECT uuid() FROM test_uuid;
```

```output
 uuid()
--------------------------------------
 12f91a52-ebba-4461-94c5-b73f0914284a
```

## See also

- [`DATE`, `TIME` and `TIMESTAMP`](../type_datetime/)
- [`TIMEUUID`](../type_uuid)
- [`UUID`](../type_uuid)
