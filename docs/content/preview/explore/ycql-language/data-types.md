---
title: Data types in YCQL
linkTitle: Data types
description: Data types in YCQL
image: /images/section_icons/secure/create-roles.png
menu:
  preview:
    identifier: explore-ycql-language-features-data-types
    parent: explore-ycql-language
    weight: 150
type: docs
---

This page describes the data types supported in YCQL, from the basic data types to collections, and user defined types.

The [JSONB document data type](../../json-support/jsonb-ycql/) is described in a separate section.

{{% explore-setup-single %}}

## Strings

The following character types are supported:

|   Type         |                      Description                          |
| :------------- | :-------------------------------------------------------- |
| VARCHAR        | String of unicode characters of unlimited length          |
| TEXT           | String of unicode characters of unlimited length          |

`varchar` and `text` are aliases.

The following Apache Cassandra character types are not supported:

|   Type         |                      Description                          |
| :------------- | :-------------------------------------------------------- |
| ASCII          | Use TEXT or VARCHAR                                       |

To test YugabyteDB support for character types, create a table that has columns with the following types specified:

```sql
CREATE KEYSPACE types_test;
USE types_test;

CREATE TABLE char_types (
  id int PRIMARY KEY,
  a TEXT,
  b VARCHAR
);
```

Insert the following rows into the table:

```sql
INSERT INTO char_types (id, a, b) VALUES (
  1, 'Data for the text column', 'Data for the varchar column'
);
```

## Numeric types

The following numeric types are supported:

|   Type         |                      Description                          |
| :------------- | :-------------------------------------------------------- |
| TINYINT        | 1-byte signed integer that has a range from -128 to 127  |
| SMALLINT       | 2-byte signed integer that has a range from -32,768 to 32,767 |
| INT \| INTEGER | 4-byte integer that has a range from -2,147,483,648 to 2,147,483,647 |
| BIGINT         | 8-byte integer that has a range from -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807 |
| VARINT         | Arbitrary-precision integer |
| FLOAT \| DOUBLE  | 64-bit, inexact, floating-point number |
| DECIMAL        | Exact, arbitrary-precision number, no upper-bound on decimal precision |

The following example creates a table with integer type columns and inserts rows into it:

```sql
CREATE TABLE albums (
  album_id BIGINT PRIMARY KEY,
  title VARCHAR,
  play_time SMALLINT,
  library_record INT
);

INSERT INTO albums (album_id, title, play_time, library_record)
values (3223372036854775808,'Funhouse', 3600,2146483645 );
```

Similarly, the following example shows how to create a table with floating-point typed columns and insert a row into that table:

```sql
CREATE TABLE floating_point_test (
  float_test FLOAT PRIMARY KEY,
  decimal_test DECIMAL
);

INSERT INTO floating_point_test (float_test, decimal_test)
VALUES (92233720368547.75807, 5.36152342);
```

## Date and time

Temporal data types allow us to store date and time data. The following date and time types are supported in YugabyteDB:

|   Type         |                      Description                          |
| :------------- | :-------------------------------------------------------- |
| DATE | stores the dates only|
| TIME | stores the time of day values with nanosecond precision|
| TIMESTAMP | stores both date and time values with milliseconds precision|

The following example creates a table with the temporal types:

```sql
CREATE TABLE temporal_types (
  date_type DATE PRIMARY KEY,
  time_type TIME,
  timestamp_type TIMESTAMP
);
```

The following example inserts a row into the table:

```sql
INSERT INTO temporal_types (
  date_type, time_type, timestamp_type)
VALUES
  ('2000-06-28', '06:23:00', '2016-06-22 19:10:25');
```

The following shows the inserted data:

```sql
ycqlsh> select * from temporal_types;
```

```output
 date_type  | time_type          | timestamp_type
------------+--------------------+---------------------------------
 2000-06-28 | 06:23:00.000000000 | 2016-06-23 00:10:25.000000+0000

(1 rows)
```

## Universally unique ID types

A universally unique identifier (or UUID) is commonly used in distributed databases for generating
unique identifiers without coordination from a central authority since that can become a bottleneck.
These IDs are then used to identify unique rows in a database table. YugabyteDB supports two
versions of UUIDs:

|   Type         |                      Description                          |
| :------------- | :-------------------------------------------------------- |
| UUID | [Version 4](https://en.wikipedia.org/wiki/Universally_unique_identifier#Version_4_(random)) UUID |
| TIMEUUID | [Version 1](https://en.wikipedia.org/wiki/Universally_unique_identifier#Version_1_(date-time_and_MAC_address)) UUID |

`TIMEUUID` is typically used when time-ordered unique identifiers are required in time-series use
cases.

The following example creates a table with the UUID types:

```sql
CREATE TABLE iot (
  sensor_id UUID,
  measurement_id TIMEUUID,
  measurement FLOAT,
  PRIMARY KEY (sensor_id, measurement_id)
);
```

The following example inserts a row into the table:

```sql
INSERT INTO iot (
  sensor_id, measurement_id, measurement)
VALUES
  (28df63b7-cc57-43cb-9752-fae69d1653da, 4eb369b0-91de-11bd-8000-000000000000, 98.4);
```

The following shows the inserted data:

```sql
ycqlsh> select * from iot;
```

```output
 sensor_id                            | measurement_id                       | measurement
--------------------------------------+--------------------------------------+-------------
 28df63b7-cc57-43cb-9752-fae69d1653da | 4eb369b0-91de-11bd-8000-000000000000 |        98.4

(1 rows)
```

## Collection types

A collection data type allows storage of multi-valued columns. YugabyteDB supports the following
types of collections:

|   Type         |                      Description                          |
| :------------- | :-------------------------------------------------------- |
| LIST | Collection of ordered elements. Allows duplicates. |
| SET | Collection of unique elements. Order may not be maintained. |
| MAP | Collection of key-value pairs. Order may not be maintained. Keys must be unique. |

The following example creates a table with the collection types:

```sql
CREATE TABLE user_profile (
  user_id UUID,
  user_name TEXT,
  recent_logins LIST<TIMESTAMP>,
  phone_numbers MAP<TEXT,TEXT>,
  account_numbers SET<TEXT>,
  PRIMARY KEY (user_id)
);
```

The following example inserts a row into the table:

```sql
INSERT INTO user_profile (
  user_id, user_name, recent_logins, phone_numbers, account_numbers)
VALUES
  (28df63b7-cc57-43cb-9752-fae69d1653da, 'John Doe', ['2023-02-03T04:05:00+0000'], {'home':'669-555-1212','work':'408-555-2121'},
  {'sa-1011212'});
```

The following shows the inserted data:

```sql
ycqlsh> select * from user_profile;
```

```output
 user_id                              | user_name | recent_logins                       | phone_numbers                                    | account_numbers
--------------------------------------+-----------+-------------------------------------+--------------------------------------------------+-----------------
 28df63b7-cc57-43cb-9752-fae69d1653da |  John Doe | ['2023-02-03 04:05:00.000000+0000'] | {'home': '669-555-1212', 'work': '408-555-2121'} |  {'sa-1011212'}

(1 rows)
```

When the user logs in again, the 'recent_logins' LIST column can be updated as shown below:

```sql
UPDATE user_profile
SET recent_logins = recent_logins + ['2023-04-05 09:15:08.000000+0000']
WHERE user_id = 28df63b7-cc57-43cb-9752-fae69d1653da;
```

```sql
ycqlsh> select * from user_profile;
```

```output
 user_id                              | user_name | recent_logins                                                          | phone_numbers                                    | account_numbers
--------------------------------------+-----------+------------------------------------------------------------------------+--------------------------------------------------+-----------------
 28df63b7-cc57-43cb-9752-fae69d1653da |  John Doe | ['2011-02-03 04:05:00.000000+0000', '2023-04-05 09:15:08.000000+0000'] | {'home': '669-555-1212', 'work': '408-555-2121'} |  {'sa-1011212'}

(1 rows)
```
The preceding example appends the new element to an existing list. Prepending is also possible, as
follows:

```sql
UPDATE user_profile
SET recent_logins = ['2023-04-05 09:15:08.000000+0000'] + recent_logins
WHERE user_id = 28df63b7-cc57-43cb-9752-fae69d1653da;
```

```sql
ycqlsh> select * from user_profile;
```

```output
 user_id                              | user_name | recent_logins                                                          | phone_numbers                                    | account_numbers
--------------------------------------+-----------+------------------------------------------------------------------------+--------------------------------------------------+-----------------
 28df63b7-cc57-43cb-9752-fae69d1653da |  John Doe | ['2023-04-05 09:15:08.000000+0000', '2011-02-03 04:05:00.000000+0000'] | {'home': '669-555-1212', 'work': '408-555-2121'} |  {'sa-1011212'}

(1 rows)
```

`SET` and `MAP` work similarly, except that these types do not have a
notion of prepending and the syntax for literals is slightly different. See [YCQL
Collections](../../../api/ycql/type_collection) for more details.

## User defined types

A user defined type is a collection of data types similar to a `struct` in a programming language.

The following example shows how to create and use a user defined type.

1. Create a user defined type.
    ```sql
    CREATE TYPE inventory_item (
       name text,
       supplier_id integer,
       price float
    );
    ```

1. Create a table with a user defined type as follows:
     ```sql
     CREATE TABLE on_hand (
        item_id UUID PRIMARY KEY,
        item inventory_item,
        count integer
     );
     ```

1. Insert a row as follows:
    ```sql
    INSERT INTO on_hand (item_id, item, count) VALUES (28df63b7-cc57-43cb-9752-fae69d1653da, {name: 'fuzzy dice', supplier_id: 42, price: 1.99}, 1000);
    ```
1. To select data from the `on_hand` example table, execute the following:
    ```sql
    SELECT * FROM on_hand WHERE item_id = 28df63b7-cc57-43cb-9752-fae69d1653da;
    ```

    Expect the following output:

    ```output
     item_id                              | item                                               | count
    --------------------------------------+----------------------------------------------------+-------
     28df63b7-cc57-43cb-9752-fae69d1653da | {name: 'fuzzy dice', supplier_id: 42, price: 1.99} |  1000
    
    (1 rows)
    ```
