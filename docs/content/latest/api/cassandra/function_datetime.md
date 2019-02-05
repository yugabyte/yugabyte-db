---
title: Date & Time Functions
summary: Functions that work on data types related to date and time.
description: Date & Time Functions
menu:
  latest:
    parent: api-cassandra
    weight: 1560
aliases:
  - /latest/api/cassandra/function_datetime
  - /latest/api/ycql/function_datetime
isTocNested: false
showAsideToc: true
---

This section covers the set of CQL builtin functions that work on the data types related to
date and time, i.e [`DATE`, `TIME`, `TIMESTAMP`](../type_datetime) or [`TIMEUUID`](../type_uuid).

## currentdate(), currenttime() and currenttimestamp()

These functions return the current system date and time in UTC time zone.

- They take in no arguments.
- The return value is a `DATE`, `TIME` or `TIMESTAMP` respectively.

### Examples

#### Insert values using currentdate(), currenttime() and currenttimestamp()
```{.sql .copy .separator-gt}
cqlsh:example> CREATE TABLE test_current (k INT PRIMARY KEY, d DATE, t TIME, ts TIMESTAMP);
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO test_current (k, d, t, ts) VALUES (1, currentdate(), currenttime(), currenttimestamp());
```

#### Comparison using currentdate() and currenttime()
```{.sql .copy .separator-gt}
cqlsh:example> SELECT * FROM test_current WHERE d = currentdate() and t < currenttime();
```
```
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
```{.sql .copy .separator-gt}
cqlsh:example> CREATE TABLE test_now (k INT PRIMARY KEY, v TIMEUUID);
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO test_now (k, v) VALUES (1, now());
```

#### Select using now()
```{.sql .copy .separator-gt}
cqlsh:example> SELECT now() FROM test_now;
```
```
 now()
---------------------------------------
 b75bfaf6-4fe9-11e8-8839-6336e659252a
```

#### Comparison using now()
```{.sql .copy .separator-gt}
cqlsh:example> SELECT v FROM test_now WHERE v < now();
```
```
 v
---------------------------------------
 71bb5104-4fe9-11e8-8839-6336e659252a
```

## todate()

This function converts a timestamp or TIMEUUID to the corresponding date.

- It takes in an argument of type `TIMESTAMP` or `TIMEUUID`. 
- The return value is a `DATE`.

```{.sql .copy .separator-gt}
cqlsh:example> CREATE TABLE test_todate (k INT PRIMARY KEY, ts TIMESTAMP);
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO test_todate (k, ts) VALUES (1, currenttimestamp());
```
```{.sql .copy .separator-gt}
cqlsh:example> SELECT todate(ts) FROM test_todate;
```
```
 todate(ts)
------------
 2018-10-09
 ```

## totimestamp()

This function converts a date or TIMEUUID to the corresponding timestamp.

- It takes in an argument of type `DATE` or `TIMEUUID`. 
- The return value is a `TIMESTAMP`.

### Examples

#### Insert values using totimestamp()
```{.sql .copy .separator-gt}
cqlsh:example> CREATE TABLE test_totimestamp (k INT PRIMARY KEY, v TIMESTAMP);
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO test_totimestamp (k, v) VALUES (1, totimestamp(now()));
```

#### Select using totimestamp()

```{.sql .copy .separator-gt}
cqlsh:example> SELECT totimestamp(now()) FROM test_totimestamp;
```
```{.text}
 totimestamp(now())
---------------------------------
 2018-05-04 22:32:56.966000+0000
```

#### Comparison using totimestamp()
```{.sql .copy .separator-gt}
cqlsh:example> SELECT v FROM test_totimestamp WHERE v < totimestamp(now());
```
```{.text}
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

```{.sql .copy .separator-gt}
cqlsh:example> CREATE TABLE test_dateof (k INT PRIMARY KEY, v TIMESTAMP);
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO test_dateof (k, v) VALUES (1, dateof(now()));
```

#### Select using dateof()
```{.sql .copy .separator-gt}
cqlsh:example> SELECT dateof(now()) FROM test_dateof;
```
```{.nohighlight}
 dateof(now())
---------------------------------
 2018-05-04 22:43:28.440000+0000
```
#### Comparison using dateof()
```{.sql .copy .separator-gt}
cqlsh:example> SELECT v FROM test_dateof WHERE v < dateof(now());
```
```{.nohighlight}
 v
---------------------------------
 2018-05-04 22:43:18.626000+0000
```

## tounixtimestamp()

This function converts TIMEUUID, date or timestamp to a unix timestamp (which is
equal to the number of millisecond since epoch Thursday, 1 January 1970). 

- It takes in an argument of type `TIMEUUID`, `DATE` or `TIMESTAMP`.
- The return value is a `BIGINT`.

### Examples

#### Insert values using tounixtimestamp()
```{.sql .copy .separator-gt}
cqlsh:example> CREATE TABLE test_tounixtimestamp (k INT PRIMARY KEY, v BIGINT);
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO test_tounixtimestamp (k, v) VALUES (1, tounixtimestamp(now()));
```

#### Select using tounixtimestamp()
```{.sql .copy .separator-gt}
cqlsh:example> SELECT tounixtimestamp(now()) FROM test_tounixtimestamp;
```
```
 tounixtimestamp(now())
------------------------
          1525473993436
```

#### Comparison using tounixtimestamp()
```{.sql .copy .separator-gt}
cqlsh:example> SELECT v from test_tounixtimestamp WHERE v < tounixtimestamp(now());
```
```
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
```{.sql .copy .separator-gt}
cqlsh:example> CREATE TABLE test_unixtimestampof (k INT PRIMARY KEY, v BIGINT);
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO test_unixtimestampof (k, v) VALUES (1, unixtimestampof(now()));
```

#### Select using unixtimestampof()
```{.sql .copy .separator-gt}
cqlsh:example> SELECT unixtimestampof(now()) FROM test_unixtimestampof;
```
```
 unixtimestampof(now())
------------------------
          1525474361676
```

#### Comparison using unixtimestampof()
```{.sql .copy .separator-gt}
cqlsh:example> SELECT v from test_unixtimestampof WHERE v < unixtimestampof(now());
```
```
 v
---------------
 1525474356781
```

## See Also

[`DATE`, `TIME` and `TIMESTAMP`](../type_datetime)
[`TIMEUUID`](../type_uuid)
[`UUID`](../type_uuid)
[Other CQL Statements](..)
