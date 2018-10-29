---
title: Date and Time Functions
summary: Functions that work on data types related to date and time.
description: Date and Time Functions.
menu:
  v1.0:
    parent: api-cassandra
    weight: 1560
---

This section covers the set of CQL builtin functions that work on the data types related to
date and time, i.e `DATE`, `TIME`, [`TIMEUUID`](../type_uuid) or [`TIMESTAMP`](../type_timestamp).

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

## totimestamp()

This function converts a TIMEUUID to the corresponding timestamp.

- It takes in an argument of type `TIMEUUID`. 
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

This function converts TIMEUUID or timestamp to a unix timestamp (which is
equal to the number of millisecond since epoch Thursday, 1 January 1970). 

- It takes in an argument of type `TIMEUUID` or type `TIMESTAMP`.
- The return value is a `INTEGER`.

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
- The return value is a `INTEGER`.

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

[`TIMESTAMP`](../type_timestamp)
[`TIMEUUID`](../type_uuid)
[`UUID`](../type_uuid)
[Other CQL Statements](..)
