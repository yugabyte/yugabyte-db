---
title: Date & Time
summary: DATE, TIME and TIMESTAMP
description: DateTime Types
menu:
  v1.0:
    parent: api-cassandra
    weight: 1450
---

## Synopsis

Datetime datatypes are used to specify data of date and time at a timezone, `DATE` for a specific day, `TIME` for time of day, and `TIMESTAMP` for the combination of both date and time.
Of the three, YugaByte currently only supports the `TIMESTAMP` type.

## Syntax
```
type_specification ::= TIMESTAMP | DATE | TIME

timestamp_format ::= date_format [ time_format ] [ timezone_format ]
date_format ::= digit digit digit digit '-' digit [ digit ] '-' digit [ digit ]
time_format ::= digit [ digit ] [ ':' digit [ digit ] [ ':' digit [digit]  [ '.' digit [ digit [ digit ] ] ] ] ] 
timezone_format ::= [ 'UTC' ] ( '+' | '-' ) digit [ digit ] ':' digit [ digit ] 
```

Where

- the `timestamp_format` given above is not the timestamp literal but is used to match text literals when converting them to `TIMESTAMP` type.

## Semantics

- Columns of type `TIMESTAMP` can be part of the `PRIMARY KEY`.
- Implicitly, value of type datetime type are neither convertible nor comparable to other datatypes.
- Values of integer and text datatypes with the correct format (given above) are convertible to datetime types.
- Supported timestamp range is from year `1900` to year `9999`.
- If not specified, the default value for hour, minute, second, and millisecond components is `0`.
- If not specified, the default timezone is UTC.

## Examples

### Using the timestamp type
```{.sql .copy .separator-gt}
cqlsh:example> CREATE TABLE sensor_data(sensor_id INT, ts TIMESTAMP, value FLOAT, PRIMARY KEY(sensor_id, ts));
```
```{.sql .copy .separator-gt}
cqlsh:example> -- Timestamp values can be given using date-time literals
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO sensor_data(sensor_id, ts, value) VALUES (1, '2017-07-04 12:30:30 UTC', 12.5);
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO sensor_data(sensor_id, ts, value) VALUES (1, '2017-07-04 12:31 UTC', 13.5);
```
Timestamp values can also be given as integers (milliseconds from epoch).
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO sensor_data(sensor_id, ts, value) VALUES (2, 1499171430000, 20);
```
```{.sql .copy .separator-gt}
cqlsh:example> SELECT * FROM sensor_data;
```
```sh
 sensor_id | ts                              | value
-----------+---------------------------------+-------
         2 | 2017-07-04 12:30:30.000000+0000 |    20
         1 | 2017-07-04 12:30:30.000000+0000 |  12.5
         1 | 2017-07-04 12:31:00.000000+0000 |  13.5
```

### Supported timestamp literals

```
'1992-06-04 12:30'
'1992-6-4 12:30'
'1992-06-04 12:30+04:00'
'1992-6-4 12:30-04:30'
'1992-06-04 12:30 UTC+04:00'
'1992-6-4 12:30 UTC-04:30'
'1992-06-04 12:30.321'
'1992-6-4 12:30.12'
'1992-06-04 12:30.321+04:00'
'1992-6-4 12:30.12-04:30'
'1992-06-04 12:30.321 UTC+04:00'
'1992-6-4 12:30.12 UTC-04:30'
'1992-06-04 12:30:45'
'1992-6-4 12:30:45'
'1992-06-04 12:30:45+04:00'
'1992-6-4 12:30:45-04:30'
'1992-06-04 12:30:45 UTC+04:00'
'1992-6-4 12:30:45 UTC-04:30'
'1992-06-04 12:30:45.321'
'1992-6-4 12:30:45.12'
'1992-06-04 12:30:45.321+04:00'
'1992-6-4 12:30:45.12-04:30'
'1992-06-04 12:30:45.321 UTC+04:00'
'1992-6-4 12:30:45.12 UTC-04:30'
'1992-06-04T12:30'
'1992-6-4T12:30'
'1992-06-04T12:30+04:00'
'1992-6-4T12:30-04:30'
'1992-06-04T12:30 UTC+04:00'
'1992-6-4T12:30TUTC-04:30'
'1992-06-04T12:30.321'
'1992-6-4T12:30.12'
'1992-06-04T12:30.321+04:00'
'1992-6-4T12:30.12-04:30'
'1992-06-04T12:30.321 UTC+04:00'
'1992-6-4T12:30.12 UTC-04:30'
'1992-06-04T12:30:45'
'1992-6-4T12:30:45'
'1992-06-04T12:30:45+04:00'
'1992-6-4T12:30:45-04:30'
'1992-06-04T12:30:45 UTC+04:00'
'1992-6-4T12:30:45 UTC-04:30'
'1992-06-04T12:30:45.321'
'1992-6-4T12:30:45.12'
'1992-06-04T12:30:45.321+04:00'
'1992-6-4T12:30:45.12-04:30'
'1992-06-04T12:30:45.321 UTC+04:00'
'1992-6-4T12:30:45.12 UTC-04:30'
'1992-06-04'
'1992-6-4'
'1992-06-04+04:00'
'1992-6-4-04:30'
'1992-06-04 UTC+04:00'
'1992-6-4 UTC-04:30'
 ```

## See Also
[Date and Time Functions](../function_datetime)
[Data Types](..#datatypes)
