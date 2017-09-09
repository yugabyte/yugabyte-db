---
title: DateTime
summary: Date and time.
toc: false
---
<style>
table {
  float: left;
}
#psyn {
  text-indent: 50px;
}
#ptodo {
  color: red
}
</style>

## Synopsis

Datetime datatypes are used to specify data of date and time at a timezone, `DATE` for a specific day, `TIME` for time of day, and `TIMESTAMP` for the combination of both date and time. If not specified, the default timezone is UTC.

## Syntax
```
type_specification ::= { TIMESTAMP | DATE | TIME }
```

## Semantics
<li>Implicitly, value of type datetime type are neither convertible nor comparable to other datatypes.</li>
<li>Value of integer and text datatypes with correct format are convertible to datetime types.</li>

## Examples

``` sql
cqlsh:example> CREATE TABLE sensor_data(sensor_id INT, ts TIMESTAMP, value FLOAT, PRIMARY KEY(sensor_id, ts));
cqlsh:example> -- Timestamp values can be given using date-time literals
cqlsh:example> INSERT INTO sensor_data(sensor_id, ts, value) VALUES (1, '2017-07-04 12:30:30 UTC', 12.5);
cqlsh:example> INSERT INTO sensor_data(sensor_id, ts, value) VALUES (1, '2017-07-04 12:31 UTC', 13.5);
cqlsh:example> -- Timestamp values can also be given as integers (milliseconds from epoch).
cqlsh:example> INSERT INTO sensor_data(sensor_id, ts, value) VALUES (2, 1499171430000, 20);
cqlsh:example> SELECT * FROM sensor_data;

 sensor_id | ts                              | value
-----------+---------------------------------+-------
         2 | 2017-07-04 12:30:30.000000+0000 |    20
         1 | 2017-07-04 12:30:30.000000+0000 |  12.5
         1 | 2017-07-04 12:31:00.000000+0000 |  13.5
```

- Samples of supported timestamp literals are shown below:

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

[Data Types](..#datatypes)
