---
title: Date and time in YSQL
headerTitle: Date and time
linkTitle: 7. Date and time
description: Learn how to work with date and time in YSQL.
menu:
  stable:
    parent: learn
    name: 7. Date and time
    identifier: date-and-time-1-ysql
    weight: 569
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="{{< relref "./date-and-time-ysql.md" >}}" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="{{< relref "./date-and-time-ycql.md" >}}" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

## Introduction

YugabyteDB has extensive date and time capabilities. Once understood, the rich functionality allows you to perform very sophisticated calculations and granular time capture.

For date and time data types, see [Data types](/preview/api/ysql/datatypes/).

The examples use the [Retail Analytics sample dataset](../../../sample-data/retail-analytics/).

## Special values

There are special values that you can reference - YugabyteDB only caters for some, other special values from PostgreSQL are not implemented in YugabyteDB, but some can be recreated if you require them. The following examples demonstrate the YSQL to select special date and time values. First start ysqlsh from the command line.

```sh
./bin/ysqlsh
```

```sql
yugabyte=# SELECT current_date, current_time, current_timestamp, now();
```

```output
 current_date |    current_time    |       current_timestamp       |              now
--------------+--------------------+-------------------------------+-------------------------------
 2019-07-09   | 00:53:13.924407+00 | 2019-07-09 00:53:13.924407+00 | 2019-07-09 00:53:13.924407+00
```

```sql
yugabyte=# SELECT make_timestamptz(1970, 01, 01, 00, 00, 00, 'UTC') as epoch;
```

```output
         epoch
------------------------
 1970-01-01 00:00:00+00
```

```sql
yugabyte=# SELECT (current_date-1)::timestamp as yesterday,
                  current_date::timestamp as today,
                  (current_date+1)::timestamp as tomorrow;
```

```output
      yesterday      |        today        |      tomorrow
---------------------+---------------------+---------------------
 2019-07-08 00:00:00 | 2019-07-09 00:00:00 | 2019-07-10 00:00:00
```

{{< note title="Note" >}}
YugabyteDB cannot create the special PostgreSQL values `infinity`, `-infinity`, and `allballs` ('allballs' is a theoretical time of "00:00:00.00 UTC").
{{< /note >}}

## Formatting

The previous examples show the default ISO format for dates and timestamps. The following examples show how you can format dates:

```sql
yugabyte=# SELECT to_char(current_timestamp, 'DD-MON-YYYY');
```

```output
   to_char
-------------
 09-JUL-2019
```

```sql
yugabyte=# SELECT to_date(to_char(current_timestamp, 'DD-MON-YYYY'), 'DD-MON-YYYY');
```

```output
  to_date
------------
 2019-07-09
```

```sql
yugabyte=# SELECT to_char(current_timestamp, 'DD-MON-YYYY HH:MI:SS PM');
```

```output
         to_char
-------------------------
 09-JUL-2019 01:50:13 AM
```

The examples use the `to_char()` function to present the date in a friendly readable format. When represented as a date or time data type, it is displayed using system settings, which is why the date representation of text `09-JUL-2019` appears as `2019-07-09`.

## Time zones

The default time zone installed with YugabyteDB is UTC (+0). To list the other time zones that are available, enter the following:

```sql
yugabyte=# SELECT * FROM pg_timezone_names;
```

```output
               name               | abbrev | utc_offset | is_dst
----------------------------------+--------+------------+--------
 W-SU                             | MSK    | 03:00:00   | f
 GMT+0                            | GMT    | 00:00:00   | f
 ROK                              | KST    | 09:00:00   | f
 UTC                              | UTC    | 00:00:00   | f
 US/Eastern                       | EDT    | -04:00:00  | t
 US/Pacific                       | PDT    | -07:00:00  | t
 US/Central                       | CDT    | -05:00:00  | t
 MST                              | MST    | -07:00:00  | f
 Zulu                             | UTC    | 00:00:00   | f
 posixrules                       | EDT    | -04:00:00  | t
 GMT                              | GMT    | 00:00:00   | f
 Etc/UTC                          | UTC    | 00:00:00   | f
 Etc/Zulu                         | UTC    | 00:00:00   | f
 Etc/Universal                    | UTC    | 00:00:00   | f
 Etc/GMT+2                        | -02    | -02:00:00  | f
 Etc/Greenwich                    | GMT    | 00:00:00   | f
 Etc/GMT+12                       | -12    | -12:00:00  | f
 Etc/GMT+8                        | -08    | -08:00:00  | f
 Etc/GMT-12                       | +12    | 12:00:00   | f
 WET                              | WEST   | 01:00:00   | t
 EST                              | EST    | -05:00:00  | f
 Australia/West                   | AWST   | 08:00:00   | f
 Australia/Sydney                 | AEST   | 10:00:00   | f
 GMT-0                            | GMT    | 00:00:00   | f
 PST8PDT                          | PDT    | -07:00:00  | t
 Hongkong                         | HKT    | 08:00:00   | f
 Singapore                        | +08    | 08:00:00   | f
 Universal                        | UTC    | 00:00:00   | f
 Arctic/Longyearbyen              | CEST   | 02:00:00   | t
 UCT                              | UCT    | 00:00:00   | f
 GMT0                             | GMT    | 00:00:00   | f
 Europe/London                    | BST    | 01:00:00   | t
 GB                               | BST    | 01:00:00   | t
 ...
(593 rows)
```

{{< note title="Note" >}}
Not all available time zones are shown; check your YSQL output to find the time zone you are interested in.
{{< /note >}}

You can set the time zone to use for your session using the `SET` command. You can `SET` the time zone using the time zone name as listed in pg_timezone_names, but not the abbreviation. You can also set the time zone to a numeric/decimal representation of the time offset. For example, -3.5 is 3 hours and 30 minutes before UTC.

{{< tip title="Tip" >}}
It seems logical to be able to set the time zone using the `UTC_OFFSET` format above. YugabyteDB allows this, however, be aware of the following behaviour if you choose this method:

When using POSIX time zone names, positive offsets are used for locations west of Greenwich. Everywhere else, YugabyteDB follows the ISO-8601 convention that positive time zone offsets are east of Greenwich. Therefore an entry of '+10:00:00' results in a time zone offset of -10 Hours as this is deemed East of Greenwich.
{{< /tip >}}

To show the current date and time of the underlying server, enter the following (note that the command uses the "Grave Accent" symbol, which is normally found below the Tilde `~` symbol on your keyboard):

```sql
yugabyte=# \echo `date`
```

```output
Tue 09 Jul 12:27:08 AEST 2019
```

The server time is not the date and time of the database. However, in a single node implementation of YugabyteDB there is a relationship between your computer's date and the database date because YugabyteDB obtains the date from the server when it is started.

The following examples explore the date and time (timestamps) in the database.

```sql
yugabyte=# SHOW timezone;
```

```output
 TimeZone
----------
 UTC
```

```sql
yugabyte=# SELECT current_timestamp;
```

```output
      current_timestamp
------------------------------
 2019-07-09 02:27:46.65152+00
```

```sql
yugabyte=# SET timezone = +1;
```

```output
SET
```

```sql
yugabyte=# SHOW timezone;
```

```output
 TimeZone
----------
 <+01>-01
```

```sql
yugabyte=# SELECT current_timestamp;
```

```output
      current_timestamp
------------------------------
 2019-07-09 03:28:11.52311+01
```

```sql
yugabyte=# SET timezone = -1.5;
```

```output
SET
```

```sql
yugabyte=# SELECT current_timestamp;
```

```output
        current_timestamp
----------------------------------
 2019-07-09 00:58:27.906963-01:30
```

```sql
yugabyte=# SET timezone = 'Australia/Sydney';
```

```output
SET
```

```sql
yugabyte=# SHOW timezone;
```

```output
     TimeZone
------------------
 Australia/Sydney
```

```sql
yugabyte=# SELECT current_timestamp;
```

```output
       current_timestamp
-------------------------------
 2019-07-09 12:28:46.610746+10
```

```sql
yugabyte=# SET timezone = 'UTC';
```

```output
SET
```

```sql
yugabyte=# SELECT current_timestamp;
```

```output
       current_timestamp
-------------------------------
 2019-07-09 02:28:57.610746+00
```

```sql
yugabyte=# SELECT current_timestamp AT TIME ZONE 'Australia/Sydney';
```

```output
          timezone
----------------------------
 2019-07-09 12:29:03.416867
```

(Note that the `AT TIME ZONE` statement above does not cater for the variants of `WITH TIME ZONE` and `WITHOUT TIME ZONE`.)

```sql
yugabyte=# SELECT current_timestamp(0);
```

```output
   current_timestamp
------------------------
 2019-07-09 03:15:38+00
```

```sql
yugabyte=# SELECT current_timestamp(2);
```

```output
     current_timestamp
---------------------------
 2019-07-09 03:15:53.07+00
```

When working with timestamps, you can control the seconds precision by specifying a value from 0 -> 6. Timestamps cannot go beyond millisecond precision, which is 1,000,000 parts to one second.

If your application assumes a local time, ensure that it issues a `SET` command to set to the correct time offset. (Daylight Savings is an advanced topic, so for the time being it is recommended to instead use the offset notation, for example, -3.5 for 3 hours and 30 minutes before UTC.)

## Timestamps

A database normally obtains its date and time from the underlying server. However, a distributed database is one synchronized database that is spread across many servers that are unlikely to have synchronized time.

For a detailed explanation of how time is obtained, refer to the blog post describing the [architecture of the storage layer](https://www.yugabyte.com/blog/distributed-postgresql-on-a-google-spanner-architecture-storage-layer/).

A simpler explanation is that the time is determined by the [tablet leader](../../../architecture/core-functions/write-path/#preparation-of-the-operation-for-replication-by-tablet-leader) of the table and this is the time used by all followers of the leader. Therefore the UTC timestamp of the underlying server can differ from the current timestamp that is used for a transaction on a particular table.

The following example assumes that you have created and connected to the `yb_demo` database with the [Retail Analytics sample dataset](../../../sample-data/retail-analytics/):

```sql
yb_demo=# SELECT to_char(max(orders.created_at), 'DD-MON-YYYY HH24:MI') AS "Last Order Date" from orders;
```

```output
  Last Order Date
-------------------
 19-APR-2020 14:07
```

```sql
yb_demo=# SELECT extract(MONTH from o.created_at) AS "Mth Num", to_char(o.created_at, 'MON') AS "Month",
          extract(YEAR from o.created_at) AS "Year", count(*) AS "Orders"
          from orders o
          where o.created_at > current_timestamp(0)
          group by 1,2,3
          order by 3 DESC, 1 DESC limit 10;
```

```output
 Mth Num | Month | Year | Orders
---------+-------+------+--------
       4 | APR   | 2020 |    344
       3 | MAR   | 2020 |    527
       2 | FEB   | 2020 |    543
       1 | JAN   | 2020 |    580
      12 | DEC   | 2019 |    550
      11 | NOV   | 2019 |    542
      10 | OCT   | 2019 |    540
       9 | SEP   | 2019 |    519
       8 | AUG   | 2019 |    566
       7 | JUL   | 2019 |    421
(10 rows)
```

```sql
yb_demo=# SELECT to_char(o.created_at, 'HH AM') AS "Popular Hours", count(*) AS "Orders"
          from orders o
          group by 1
          order by 2 DESC
          limit 4;
```

```output
 Popular Hours | Orders
---------------+--------
 12 PM         |    827
 11 AM         |    820
 03 PM         |    812
 08 PM         |    812
(4 rows)
```

```sql
yb_demo=# update orders
          set created_at = created_at + ((floor(random() * (25-2+2) + 2))::int * interval '1 day 14 hours');
```

```output
UPDATE 18760
```

```sql
yb_demo=# SELECT to_char(o.created_at, 'Day') AS "Top Day",
          count(o.*) AS "SALES"
          from orders o
          group by 1
          order by 2 desc;
```

```output
Top Day  | SALES
-----------+---------
 Monday    |    2786
 Tuesday   |    2737
 Saturday  |    2710
 Wednesday |    2642
 Friday    |    2634
 Sunday    |    2630
 Thursday  |    2621
(7 rows)
```

```sql
yb_demo=# create table order_deliveries (
          order_id bigint,
          creation_date date DEFAULT current_date,
          delivery_date timestamptz);
```

```output
CREATE TABLE
```

```sql
yb_demo=# insert into order_deliveries
          (order_id, delivery_date)
          SELECT o.id, o.created_at + ((floor(random() * (25-2+2) + 2))::int * interval '1 day 3 hours')
          from orders o
          where o.created_at < current_timestamp - (20 * interval '1 day');
```

```output
INSERT 0 12268
```

```sql
yb_demo=# SELECT * from order_deliveries limit 5;
```

```output
 order_id | creation_date |       delivery_date
----------+---------------+----------------------------
     5636 | 2019-07-09    | 2017-01-06 03:06:01.071+00
    10990 | 2019-07-09    | 2018-12-16 12:02:56.169+00
    13417 | 2019-07-09    | 2018-06-26 09:28:02.153+00
     9367 | 2019-07-09    | 2017-05-21 06:49:42.298+00
    13954 | 2019-07-09    | 2019-02-08 04:07:01.457+00
(5 rows)
```

```sql
yb_demo=# SELECT d.order_id, to_char(o.created_at, 'DD-MON-YYYY HH AM') AS "Ordered",
          to_char(d.delivery_date, 'DD-MON-YYYY HH AM') AS "Delivered",
          d.delivery_date - o.created_at AS "Delivery Days"
          from orders o, order_deliveries d
          where o.id = d.order_id
          and d.delivery_date - o.created_at > interval '15 days'
          order by d.delivery_date - o.created_at DESC, d.delivery_date DESC limit 10;
```

```output
 order_id |      Ordered      |     Delivered     |  Delivery Days
----------+-------------------+-------------------+------------------
    10984 | 12-JUN-2019 08 PM | 07-JUL-2019 02 AM | 24 days 06:00:00
     6263 | 01-JUN-2019 03 AM | 25-JUN-2019 09 AM | 24 days 06:00:00
    10498 | 18-MAY-2019 01 AM | 11-JUN-2019 07 AM | 24 days 06:00:00
    14996 | 14-MAR-2019 05 PM | 08-APR-2019 12 AM | 24 days 06:00:00
     6841 | 06-FEB-2019 01 AM | 02-MAR-2019 07 AM | 24 days 06:00:00
    10977 | 11-MAY-2019 01 PM | 03-JUN-2019 07 PM | 23 days 06:00:00
    14154 | 09-APR-2019 01 PM | 02-MAY-2019 07 PM | 23 days 06:00:00
     6933 | 31-MAY-2019 05 PM | 23-JUN-2019 12 AM | 22 days 06:00:00
     5289 | 04-MAY-2019 04 PM | 26-MAY-2019 10 PM | 22 days 06:00:00
    10226 | 01-MAY-2019 06 AM | 23-MAY-2019 12 PM | 22 days 06:00:00
(10 rows)
```

Your output may differ slightly as the `RANDOM()` function is used to set the `delivery_date` in the new `order_deliveries` table.

You can use views of the YugabyteDB Data Catalogs to create data that is already prepared and formatted for your application code so that your SQL is simpler. The following example shows how you can nominate a shortlist of time zones that are formatted and ready to use for display purposes:

```sql
yb_demo=# CREATE OR REPLACE VIEW TZ AS
          SELECT '* Current time' AS "tzone", '' AS "offset", to_char(current_timestamp AT TIME ZONE 'Australia/Sydney', 'Dy dd-Mon-yy hh:mi PM') AS "Local Time"
          UNION
          SELECT x.name AS "tzone",
          left(x.utc_offset::text, 5) AS "offset",
          to_char(current_timestamp AT TIME ZONE x.name, 'Dy dd-Mon-yy hh:mi PM') AS "Local Time"
          from pg_catalog.pg_timezone_names x
          where  x.name like 'Australi%' or name in('Singapore', 'NZ', 'UTC')
          order by 1 asc;
```

```output
CREATE VIEW
```

```sql
yb_demo=# SELECT * from tz;
```

```output
         tzone         | offset |       Local Time
-----------------------+--------+------------------------
 * Current time        |        | Wed 10-Jul-19 11:49 AM
 Australia/ACT         | 10:00  | Wed 10-Jul-19 11:49 AM
 Australia/Adelaide    | 09:30  | Wed 10-Jul-19 11:19 AM
 Australia/Brisbane    | 10:00  | Wed 10-Jul-19 11:49 AM
 Australia/Broken_Hill | 09:30  | Wed 10-Jul-19 11:19 AM
 Australia/Canberra    | 10:00  | Wed 10-Jul-19 11:49 AM
 Australia/Currie      | 10:00  | Wed 10-Jul-19 11:49 AM
 Australia/Darwin      | 09:30  | Wed 10-Jul-19 11:19 AM
 Australia/Eucla       | 08:45  | Wed 10-Jul-19 10:34 AM
 Australia/Hobart      | 10:00  | Wed 10-Jul-19 11:49 AM
 Australia/LHI         | 10:30  | Wed 10-Jul-19 12:19 PM
 Australia/Lindeman    | 10:00  | Wed 10-Jul-19 11:49 AM
 Australia/Lord_Howe   | 10:30  | Wed 10-Jul-19 12:19 PM
 Australia/Melbourne   | 10:00  | Wed 10-Jul-19 11:49 AM
 Australia/NSW         | 10:00  | Wed 10-Jul-19 11:49 AM
 Australia/North       | 09:30  | Wed 10-Jul-19 11:19 AM
 Australia/Perth       | 08:00  | Wed 10-Jul-19 09:49 AM
 Australia/Queensland  | 10:00  | Wed 10-Jul-19 11:49 AM
 Australia/South       | 09:30  | Wed 10-Jul-19 11:19 AM
 Australia/Sydney      | 10:00  | Wed 10-Jul-19 11:49 AM
 Australia/Tasmania    | 10:00  | Wed 10-Jul-19 11:49 AM
 Australia/Victoria    | 10:00  | Wed 10-Jul-19 11:49 AM
 Australia/West        | 08:00  | Wed 10-Jul-19 09:49 AM
 Australia/Yancowinna  | 09:30  | Wed 10-Jul-19 11:19 AM
 NZ                    | 12:00  | Wed 10-Jul-19 01:49 PM
 Singapore             | 08:00  | Wed 10-Jul-19 09:49 AM
 UTC                   | 00:00  | Wed 10-Jul-19 01:49 AM
(27 rows)
```

Assuming that you chose the time zones that interest you, your results should be different to those shown above.

## Date and time intervals

An interval is a data type that describes an increment of time. An interval allows you to show the difference between two timestamps or to create a new timestamp by adding or subtracting a particular unit of measure. Consider the following examples:

```sql
yugabyte=# SELECT current_timestamp AS "Current Timestamp",
           current_timestamp + (10 * interval '1 min') AS "Plus 10 Mins",
           current_timestamp + (10 * interval '3 min') AS "Plus 30 Mins",
           current_timestamp + (10 * interval '2 hour') AS "Plus 20 hours",
           current_timestamp + (10 * interval '1 month') AS "Plus 10 Months"
```

```output
       Current Timestamp       |         Plus 10 Mins          |         Plus 30 Mins          |         Plus 20 hours         |        Plus 10 Months
-------------------------------+-------------------------------+-------------------------------+-------------------------------+-------------------------------
 2019-07-09 05:08:58.859123+00 | 2019-07-09 05:18:58.859123+00 | 2019-07-09 05:38:58.859123+00 | 2019-07-10 01:08:58.859123+00 | 2020-05-09 05:08:58.859123+00
```

```sql
yugabyte=# SELECT current_time::time(0), time '05:00' + interval '5 hours 7 mins' AS "New time";
```

```output
 current_time | New Time
--------------+----------
 05:09:24     | 10:16:24
```

```sql
yugabyte=# SELECT current_date - date '01-01-2019' AS "Day of Year(A)", current_date - date_trunc('year', current_date) AS "Day of Year(B)";
```

```output
 Day of Year(A) | Day of Year(B)
----------------+----------------
            189 | 189 days
```

```sql
yugabyte=# SELECT timestamp '2019-07-09 10:00:00.000000+00' - timestamp '2019-07-09 09:00:00.000000+00' AS "Time Difference";
```

```output
 Time Difference
-----------------
 01:00:00
```

```sql
yugabyte=# SELECT timestamp '2019-07-09 10:00:00.000000+00' - timestamptz '2019-07-09 10:00:00.000000+00' AS "Time Offset";
```

```output
 Time Offset
-------------
 00:00:00
```

```sql
yugabyte=# SELECT timestamp '2019-07-09 10:00:00.000000+00' - timestamptz '2019-07-09 10:00:00.000000EST' AS "Time Offset";
```

```output
 Time Offset
-------------
 -05:00:00
```

```sql
yugabyte=# SELECT timestamp '2019-07-09 10:00:00.000000+00' - timestamptz '2019-07-08 10:00:00.000000EST' AS "Time Offset";
```

```output
 Time Offset
-------------
 19:00:00
```

```sql
yugabyte=# SELECT timestamp '2019-07-09 10:00:00.000000+00' - timestamptz '2019-07-07 10:00:00.000000EST' AS "Time Offset";
```

```output
  Time Offset
----------------
 1 day 19:00:00
```

```sql
yugabyte=# SELECT age(timestamp '2019-07-09 10:00:00.000000+00', timestamptz '2019-07-07 10:00:00.000000EST') AS "Age Diff";
```

```output
    Age Diff
----------------
 1 day 19:00:00
```

```sql
yugabyte=# SELECT (extract('days' from age(timestamp '2019-07-09 10:00:00.000000+00', timestamptz '2019-07-07 10:00:00.000000EST'))*24)+
           (extract('hours' from age(timestamp '2019-07-09 10:00:00.000000+00', timestamptz '2019-07-07 10:00:00.000000EST'))) AS "Hours Diff";
```

```output
 Hours Diff
------------
         43
```

The above shows that date and time manipulation can be achieved in several ways. It is important to note that some outputs are of type `INTEGER`, whilst others are of type `INTERVAL` (not text as they may appear). The final YSQL above for "Hours Diff" uses the output of `EXTRACT` which produces an `INTEGER` so that it may be multiplied by the hours per day, whereas the `EXTRACT` function itself requires either a `INTERVAL` or `TIMESTAMP(TZ)` data type as its input.

Be sure to cast your values thoroughly. Casts can be done for time(tz), date and timestamp(tz) like `MY_VALUE::timestamptz`.

{{< note title="Note" >}}
The `EXTRACT` command is preferred to `DATE_PART`.
{{< /note >}}

## Manipulating using truncation

The `DATE_TRUNC` command is used to 'floor' the timestamp to a particular unit. The example assumes that you have created and connected to the `yb_demo` database with the [Retail Analytics sample dataset](../../../sample-data/retail-analytics/).

```sql
yb_demo=# SELECT date_trunc('hour', current_timestamp);
```

```output
       date_trunc
------------------------
 2019-07-09 06:00:00+00
(1 row)
```

```sql
yb_demo=# SELECT to_char((date_trunc('month', generate_series)::date)-1, 'DD-MON-YYYY') AS "Last Day of Month"
          from generate_series(current_date-(365-1), current_date, '1 month');
```

```output
 Last Day of Month
-------------------
 30-JUN-2018
 31-JUL-2018
 31-AUG-2018
 30-SEP-2018
 31-OCT-2018
 30-NOV-2018
 31-DEC-2018
 31-JAN-2019
 28-FEB-2019
 31-MAR-2019
 30-APR-2019
 31-MAY-2019
(12 rows)
```

```sql
yb_demo=# SELECT date_trunc('days', age(created_at)) AS "Product Age" from products order by 1 desc limit 10;
```

```output
      Product Age
------------------------
 3 years 2 mons 12 days
 3 years 2 mons 10 days
 3 years 2 mons 6 days
 3 years 2 mons 4 days
 3 years 1 mon 28 days
 3 years 1 mon 27 days
 3 years 1 mon 15 days
 3 years 1 mon 9 days
 3 years 1 mon 9 days
 3 years 1 mon
(10 rows)
```

## Bringing it all together

A common requirement is to find out the date of next Monday; for example, that might be the first day of the new week for scheduling purposes. This can be achieved in many ways. The following illustrates the chaining together of different date and time operators and functions to achieve the result you want:

```sql
yugabyte=# SELECT to_char(current_date, 'Day, DD-MON-YYYY') AS "Today",
           to_char((current_timestamp AT TIME ZONE 'Australia/Sydney')::date +
           (7-(extract('isodow' from current_timestamp AT TIME ZONE 'Australia/Sydney'))::int + 1),
           'Day, DD-MON-YYYY') AS "Start of Next Week";
```

```output
         Today          |   Start of Next Week
------------------------+------------------------
 Tuesday  , 09-JUL-2019 | Monday   , 15-JUL-2019
```

The above approach is to `EXTRACT` the current day of the week as an integer. As today is a Tuesday, the result will be 2. As you know there are 7 days per week, you need to target a calculation that has a result of 8, being 1 day more than the 7th day. We use this to calculate how many days to add to the current date (7 days - 2 + 1 day) to arrive at the next Monday which is day of the week (ISO dow) #1. The addition of the `AT TIME ZONE` is purely illustrative and would not impact the result because you are dealing with days, and the time zone difference is only +10 hours, therefore it does not affect the date. However, if you are working with hours or smaller, then the time zone can potentially have a bearing on your result.

{{< tip title="Fun Fact" >}}
For the very curious, why is there a gap after 'Tuesday' and 'Monday' in the example above? All 'Day' values are space padded to 9 characters. You could use string functions to remove the extra spaces if needed for formatting purposes or you could do a trimmed `TO_CHAR` for the 'Day' then concatenate with a comma and another `TO_CHAR` for the 'DD-MON-YYYY'.
{{< /tip >}}

## Ambiguity - using DateStyle

People in different locations of the world are familiar with local representations of dates. Times are reasonably similar, but dates can differ. The USA uses 3/5/19, whereas in Australia you would use 5/3/19, and in Europe either 5.3.19 or 5/3/19. What is the date in question? 5th March, 2019.

YugabyteDB has the `DateStyle` setting that you apply to your session so that ambiguous dates can be determined and the display of dates in YSQL can be defaulted to a particular format.

By default, YugabyteDB uses the ISO Standard of YYYY-MM-DD HH24:MI:SS. Other settings you can use are 'SQL', 'German', and 'Postgres'. These are all used in the following examples.

All settings except ISO allow you specify whether a Day appears before or after the Month. Therefore, a setting of 'DMY' results in 3/5 being 3rd May, whereas 'MDY' results in 5th March.

If you are reading dates as text fields from a file or any source that is not a YugabyteDB date or timestamp data type, then it is very important that you set your DateStyle properly unless you are very specific on how to convert a text field to a date - an example of which is included below.

Note that YugabyteDB always interprets '6/6' as 6th June, and '13/12' as 13th December (because the month cannot be 13), but what about '6/12'? Let's work through some examples in YSQL.

```sql
yugabyte=# SHOW DateStyle;
```

```output
 DateStyle
-----------
 ISO, DMY
```

```sql
yugabyte=# SELECT current_date, current_time(0), current_timestamp(0);
```

```output
 current_date | current_time |   current_timestamp
--------------+--------------+------------------------
 2019-07-09   | 20:26:28+00  | 2019-07-09 20:26:28+00
```

```sql
yugabyte=# SET DateStyle = 'SQL, DMY';
```

```output
SET
```

```sql
yugabyte=# SELECT current_date, current_time(0), current_timestamp(0);
```

```output
 current_date | current_time |    current_timestamp
--------------+--------------+-------------------------
 09/07/2019   | 20:26:48+00  | 09/07/2019 20:26:48 UTC
```

```sql
yugabyte=# SET DateStyle = 'SQL, MDY';
```

```output
SET
```

```sql
yugabyte=# SELECT current_date, current_time(0), current_timestamp(0);
```

```output
 current_date | current_time |    current_timestamp
--------------+--------------+-------------------------
 07/09/2019   | 20:27:04+00  | 07/09/2019 20:27:04 UTC
```

```sql
yugabyte=# SET DateStyle = 'German, DMY';
```

```output
SET
```

```sql
yugabyte=# SELECT current_date, current_time(0), current_timestamp(0);
```

```output
 current_date | current_time |    current_timestamp
--------------+--------------+-------------------------
 09.07.2019   | 20:27:30+00  | 09.07.2019 20:27:30 UTC
```

```sql
yugabyte=# SET DateStyle = 'Postgres, DMY';
```

```output
SET
```

```sql
yugabyte=# SELECT current_date, current_time(0), current_timestamp(0);
```

```output
 current_date | current_time |      current_timestamp
--------------+--------------+------------------------------
 09-07-2019   | 20:28:07+00  | Tue 09 Jul 20:28:07 2019 UTC
```

```sql
yugabyte=# SET DateStyle = 'Postgres, MDY';
```

```output
SET
```

```sql
yugabyte=# SELECT current_date, current_time(0), current_timestamp(0);
```

```output
 current_date | current_time |      current_timestamp
--------------+--------------+------------------------------
 07-09-2019   | 20:28:38+00  | Tue Jul 09 20:28:38 2019 UTC
```

```sql
yugabyte=# SELECT '01-01-2019'::date;
```

```output
    date
------------
 01-01-2019
```

```sql
yugabyte=# SELECT to_char('01-01-2019'::date, 'DD-MON-YYYY');
```

```output
   to_char
-------------
 01-JAN-2019
```

```sql
yugabyte=# SELECT to_char('05-03-2019'::date, 'DD-MON-YYYY');
```

```output
   to_char
-------------
 03-MAY-2019
```

The following example illustrates the difficulty that can occur with dates:

```sql
yugabyte=# SET DateStyle = 'Postgres, DMY';
```

```output
SET
```

```sql
yugabyte=# SELECT to_char('05-03-2019'::date, 'DD-MON-YYYY');
```

```output
   to_char
-------------
 05-MAR-2019
```

The system expects a 'DMY' value, but the source is in the format 'MDY'. YugabyteDB doesn't know how to convert ambiguous cases, so be explicit as follows:

```sql
yugabyte=# SELECT to_char(to_date('05-03-2019', 'MM-DD-YYYY'), 'DD-MON-YYYY');
```

```output
   to_char
-------------
 03-MAY-2019
```

It is recommended to pass all text representations of date and time data types through a `TO_DATE` or `TO_TIMESTAMP` function. There is no 'to_time' function as its format is always fixed of 'HH24:MI:SS.ms', therefore be careful of AM/PM times and your milliseconds can also be thousandths of a second, so either 3 or 6 digits should be supplied.

## Getting dirty - into the logs you go

{{< note title="Note" >}}
The following is for those interested in some of the finer points of control.
{{< /note >}}

YugabyteDB has inherited a lot of capabilities similar to the PostgreSQL SQL API, and this explains why when you start to look under the hood, it is looks very much like PostgreSQL.

YugabyteDB tracks its settings in its catalog. The following example queries some relevant settings and transforms the layout of the query results using the `Expanded display` setting. This can be done in any database.

```sql
yugabyte=# \x on
```

```output
Expanded display is on.
```

```sql
yugabyte=# SELECT name, short_desc, coalesce(setting, reset_val) AS "setting_value", sourcefile
          from pg_catalog.pg_settings
          where name in('log_timezone', 'log_directory', 'log_filename', 'lc_time')
          order by name asc;
```

```output
-[ RECORD 1 ]-+----------------------------------------------------------------
name          | lc_time
short_desc    | Sets the locale for formatting date and time values.
setting_value | en_US.UTF-8
sourcefile    | /home/xxxxx/yugabyte-data/node-1/disk-1/pg_data/postgresql.conf
-[ RECORD 2 ]-+----------------------------------------------------------------
name          | log_directory
short_desc    | Sets the destination directory for log files.
setting_value | /home/xxxxx/yugabyte-data/node-1/disk-1/yb-data/tserver/logs
sourcefile    |
-[ RECORD 3 ]-+----------------------------------------------------------------
name          | log_filename
short_desc    | Sets the file name pattern for log files.
setting_value | postgresql-%Y-%m-%d_%H%M%S.log
sourcefile    |
-[ RECORD 4 ]-+----------------------------------------------------------------
name          | log_timezone
short_desc    | Sets the time zone to use in log messages.
setting_value | UTC
sourcefile    | /home/xxxxx/yugabyte-data/node-1/disk-1/pg_data/postgresql.conf
```

```sql
yugabyte=# \x off
```

Using the `log_directory` and `log_filename` references, you can find the YugabyteDB log to examine the timestamps being inserted into the logs. These are all UTC timestamps and should remain that way.

You can see that the `lc_time` setting is currently UTF and the file the setting is obtained from is listed. Opening that file **as sudo/superuser**, you see contents that look like the below (search for 'datestyle'):

```output.sh
# - Locale and Formatting -

datestyle = 'iso, mdy'
#intervalstyle = 'postgres'
timezone = 'UTC'
#timezone_abbreviations = 'Default'     # Select the set of available time zone
                                        # abbreviations.  Currently, there are
                                        #   Default
                                        #   Australia (historical usage)
                                        #   India
                                        # You can create your own file in
                                        # share/timezonesets/.
#extra_float_digits = 0                 # min -15, max 3
#client_encoding = sql_ascii            # actually, defaults to database
                                        # encoding

# These settings are initialized by initdb, but they can be changed.
lc_messages = 'en_US.UTF-8'                     # locale for system error message
                                        # strings
lc_monetary = 'en_US.UTF-8'                     # locale for monetary formatting
lc_numeric = 'en_US.UTF-8'                      # locale for number formatting
lc_time = 'en_US.UTF-8'                         # locale for time formatting

# default configuration for text search
default_text_search_config = 'pg_catalog.english'
```

Make a backup of the original file and then change `datestyle = 'SQL, DMY'`, `timezone = 'GB'` (or any other time zone **name** you prefer) and save the file. You need to restart your YugabyteDB cluster for the changes to take effect.

After the cluster is running as expected, do the following:

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh (11.2)
Type "help" for help.
```

```sql
yugabyte=# SHOW timezone;
```

```output
 TimeZone
----------
 GB
```

```sql
yugabyte=# SELECT current_date;
```

```output
 current_date
--------------
 09/07/2019
```

You don't need to make those settings each time you enter YSQL. However, applications should not rely upon these settings, they should always `SET` their requirements before submitting their SQL. These settings should only be used by 'casual querying' such as you are doing now.

## Conclusion

As illustrated, dates and times is a comprehensive area that is well addressed by PostgreSQL and hence by YSQL in YugabyteDB. All of the date-time data types are implemented, and the vast majority of methods, operators, and special values are available. The functionality is complex enough for you to be able to code any shortfalls that you find in the YSQL implementation of its SQL API.
