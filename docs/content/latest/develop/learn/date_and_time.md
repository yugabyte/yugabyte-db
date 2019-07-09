
## Introduction
YugaByte DB has extensive date and time capability that may be daunting for the new user. Once understood, the rich functionality will allow you to perform very sophisticated calculations and granular time capture.

Refer to the data types on https://docs.yugabyte.com/latest/api/ysql/datatypes/ for date and time types.

## Special Values
There are special values that you can reference - YugaByte DB only caters for some, other special values from postgresql are not implemented in YugaByte, but some can be recreated if you require them. Below is YSQL to select special date and time values. First start ysql from your command line.

```sh
./bin/ysqlsh
```

```sql
postgres=# select current_date, current_time, current_timestamp, now();
 current_date |    current_time    |       current_timestamp       |              now              
--------------+--------------------+-------------------------------+-------------------------------
 2019-07-09   | 00:53:13.924407+00 | 2019-07-09 00:53:13.924407+00 | 2019-07-09 00:53:13.924407+00
(1 row)

postgres=# select make_timestamptz(1970, 01, 01, 00, 00, 00, 'UTC') as epoch;
         epoch          
------------------------
 1970-01-01 00:00:00+00
(1 row)

postgres=# select (current_date-1)::timestamp as yesterday, current_date::timestamp as today, (current_date+1)::timestamp as tomorrow;

      yesterday      |        today        |      tomorrow       
---------------------+---------------------+---------------------
 2019-07-08 00:00:00 | 2019-07-09 00:00:00 | 2019-07-10 00:00:00
(1 row)

```

Note that YugaByte cannot create the special values of `infinity`, `-infinity` and `allballs` that can be found in postgresql. If you are wondering, 'allballs' is a theoretical time of "00:00:00.00 UTC".

## Current Timestamp in a Distributed Database
A database normally obtains its date and time from the underlying server. However, in the case of a distributed database, it is one synchronized database that is spread across many servers that are unlikely to have synchronized time.

A detailed explanation of how time is obtained can be found at https://blog.yugabyte.com/distributed-postgresql-on-a-google-spanner-architecture-storage-layer/

A simpler explanation is that the time is determined by the 'Shard Leader' of the table and this is the time used by all followers of the leader. Therefore there could be differences to the UTC timestamp of the underlying server to the current timestamp that is used for a transaction on a particular table.

## Formatting
Date formatting is an important aspect. As you can see above, the examples show the default ISO format for dates and timestamps. We will now do some formatting of dates.

```sql
postgres=# select to_char(current_timestamp, 'DD-MON-YYYY');
   to_char   
-------------
 09-JUL-2019
(1 row)

postgres=# select to_date(to_char(current_timestamp, 'DD-MON-YYYY'), 'DD-MON-YYYY');
  to_date   
------------
 2019-07-09
(1 row)
postgres=# select to_char(current_timestamp, 'DD-MON-YYYY HH:MI:SS PM');
         to_char         
-------------------------
 09-JUL-2019 01:50:13 AM
(1 row)
```

## Timezones
Thus far, we have been operating with the default timezone installed for YugaByte being UTC (+0). Lets select what timezones are available from YugaByte:

```sql
postgres=# select * from pg_timezone_names;
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
 Australia/Brisbane               | AEST   | 10:00:00   | f
 Australia/Victoria               | AEST   | 10:00:00   | f
 Australia/NSW                    | AEST   | 10:00:00   | f
 Australia/North                  | ACST   | 09:30:00   | f
 Australia/Perth                  | AWST   | 08:00:00   | f
 Australia/Melbourne              | AEST   | 10:00:00   | f
 Australia/West                   | AWST   | 08:00:00   | f
 Australia/Sydney                 | AEST   | 10:00:00   | f
 GMT-0                            | GMT    | 00:00:00   | f
 PST8PDT                          | PDT    | -07:00:00  | t
 Hongkong                         | HKT    | 08:00:00   | f
 Singapore                        | +08    | 08:00:00   | f
 Universal                        | UTC    | 00:00:00   | f
 Arctic/Longyearbyen              | CEST   | 02:00:00   | t
 NZ                               | NZST   | 12:00:00   | f
 UCT                              | UCT    | 00:00:00   | f
 GMT0                             | GMT    | 00:00:00   | f
 Europe/London                    | BST    | 01:00:00   | t
 GB                               | BST    | 01:00:00   | t
 Turkey                           | +03    | 03:00:00   | f
 Japan                            | JST    | 09:00:00   | f
 ...
(593 rows)
```
**Not all 593 rows are shown**, so don't be concerned if the timezone you want is not there. Check your YSQL output to find the timezone you are interested in. What has been left in the results above is that there is a lot of inconsistency in the naming convention and definition of the timezones, this is not the doing of YugaByte!

You can set the timezone to use for your session using the `SET` command. You can `SET` timezone using the timezone name as listed in pg_timezone_names, but not the abbreviation. You can also set the timezone to a decimal representation of the time offset. For example, -3.5 is 3 hours and 30 minutes before UTC. Below is some examples for you to follow in YSQL:

```sql
postgres=# SHOW timezone;
 TimeZone 
----------
 UTC
(1 row)

postgres=# select current_timestamp;
      current_timestamp       
------------------------------
 2019-07-09 02:27:46.65152+00
(1 row)

postgres=# SET timezone = +1;
SET
postgres=# SHOW timezone;
 TimeZone 
----------
 <+01>-01
(1 row)

postgres=# select current_timestamp;
      current_timestamp       
------------------------------
 2019-07-09 03:28:11.52311+01
(1 row)

postgres=# SET timezone = -1.5;
SET
postgres=# select current_timestamp;
        current_timestamp         
----------------------------------
 2019-07-09 00:58:27.906963-01:30
(1 row)

postgres=# SET timezone = 'Australia/Sydney';
SET
postgres=# SHOW timezone;
     TimeZone     
------------------
 Australia/Sydney
(1 row)

postgres=# select current_timestamp;
       current_timestamp       
-------------------------------
 2019-07-09 12:28:46.610746+10
(1 row)

postgres=# SET timezone = 'UTC';
SET
postgres=# select current_timestamp;
       current_timestamp       
-------------------------------
 2019-07-09 02:28:57.610746+00
(1 row)

postgres=# select current_timestamp AT TIME ZONE 'Australia/Sydney';
          timezone          
----------------------------
 2019-07-09 12:29:03.416867
(1 row)

postgres=# select current_timestamp(0);
   current_timestamp    
------------------------
 2019-07-09 03:15:38+00
(1 row)

yb_demo=# select current_timestamp(2);
     current_timestamp     
---------------------------
 2019-07-09 03:15:53.07+00
(1 row)
```

Therefore, if your application assumes a local time, ensure that it issues a `SET` command to set to the correct time offset. Note that Daylight Savings is quite an advanced topic, so for the time being it is recommended to instead use the offset notation, for example -3.5 for 3 hours and 30 minutes before UTC.

Note that the `AT TIME ZONE` statement above does not cater for the variants of `WITH TIME ZONE` and `WITHOUT TIME ZONE`.

## Timestamps and YSQL
Lets start working with dates and timestamps. The following assumes that you have installed the yb_demo database and its demo data.

```sh
./bin/ysqlsh
```
```sql
postgres=# \c yb_demo
You are now connected to database "yb_demo" as user "postgres".

yb_demo=# select to_char(max(orders.created_at), 'DD-MON-YYYY HH24:MI') AS "Last Order Date" from orders;
  Last Order Date  
-------------------
 19-APR-2020 14:07
(1 row)

yb_demo=# select extract(MONTH from o.created_at) AS "Mth Num", to_char(o.created_at, 'MON') AS "Month",
          extract(YEAR from o.created_at) AS "Year", count(*) AS "Orders"
          from orders o 
          where o.created_at > current_timestamp(0)
          group by 1,2,3
          order by 3 DESC, 1 DESC limit 10;
          
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

yb_demo=# select to_char(o.created_at, 'HH AM') AS "Popular Hours", count(*) AS "Orders"
          from orders o
          group by 1
          order by 2 DESC
          limit 4;

 Popular Hours | Orders 
---------------+--------
 12 PM         |    827
 11 AM         |    820
 03 PM         |    812
 08 PM         |    812
(4 rows)

yb_demo=# update orders
          set created_at = created_at + ((floor(random() * (25-2+2) + 2))::int * interval '1 day 14 hours');

UPDATE 18760

yb_demo=# select to_char(o.created_at, 'Day') AS "Top Day", 
          count(o.*) AS "SALES"
          from orders o                                                                            
          group by 1 
          order by 2 desc;

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

yb_demo=# create table order_deliveries (
          order_id bigint,                                                               
          creation_date date DEFAULT current_date,
          delivery_date timestamptz);
          
CREATE TABLE
yb_demo=# insert into order_deliveries
          (order_id, delivery_date)
          select o.id, o.created_at + ((floor(random() * (25-2+2) + 2))::int * interval '1 day 3 hours')
          from orders o
          where o.created_at < current_timestamp - (20 * interval '1 day');

INSERT 0 12268
yb_demo=# select * from order_deliveries limit 5;

 order_id | creation_date |       delivery_date        
----------+---------------+----------------------------
     5636 | 2019-07-09    | 2017-01-06 03:06:01.071+00
    10990 | 2019-07-09    | 2018-12-16 12:02:56.169+00
    13417 | 2019-07-09    | 2018-06-26 09:28:02.153+00
     9367 | 2019-07-09    | 2017-05-21 06:49:42.298+00
    13954 | 2019-07-09    | 2019-02-08 04:07:01.457+00
(5 rows)

yb_demo=# select d.order_id, to_char(o.created_at, 'DD-MON-YYYY HH AM') AS "Ordered",
          to_char(d.delivery_date, 'DD-MON-YYYY HH AM') AS "Delivered",
          d.delivery_date - o.created_at AS "Delivery Days"
          from orders o, order_deliveries d
          where o.id = d.order_id
          and d.delivery_date - o.created_at > interval '15 days'
          order by d.delivery_date - o.created_at DESC, d.delivery_date DESC limit 10;
          
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
Note that your data will be slightly different as we used a `random()` function for setting the delivery_date in the new order_deliveries table.

## Date/Time Intervals
You will see that the above has references to `INTERVAL`. An interval is a data type that describes an increment of time. An interval allows you to show the difference between two timestamps or to create a new timestamp by adding or subtracting a particular unit of measure. Some examples are:

```sql
postgres=# select current_timestamp;
       current_timestamp       
-------------------------------
 2019-07-09 05:07:47.921581+00
(1 row)

postgres=# select current_timestamp + (10 * interval '1 min') AS "New Timestamp";
        New Timestamp            
-------------------------------
 2019-07-09 05:18:12.113374+00
(1 row)

postgres=# select current_timestamp + (10 * interval '3 min') AS "New Timestamp";
        New Timestamp            
-------------------------------
 2019-07-09 05:38:19.176688+00
(1 row)

postgres=# select current_timestamp + (10 * interval '2 hour') AS "New Timestamp";
        New Timestamp            
-------------------------------
 2019-07-10 01:08:37.929069+00
(1 row)

postgres=# select current_timestamp + (10 * interval '1 month') AS "New Timestamp";
        New Timestamp            
-------------------------------
 2020-05-09 05:08:49.104971+00
(1 row)

postgres=# select time '05:00' + interval '5 hours' AS "My new time";
 My new time 
-------------
 10:00:00
(1 row)

postgres=# select current_date - date '01-01-2019' AS "Day of Year";
 Day of Year 
-------------
         189
(1 row)

postgres=# select current_timestamp;
       current_timestamp       
-------------------------------
 2019-07-09 05:16:31.707298+00
(1 row)

postgres=# select timestamp '2019-07-09 10:00:00.000000+00' - timestamp '2019-07-09 09:00:00.000000+00' AS "Time Difference";
 Time Difference 
-----------------
 01:00:00
(1 row)

postgres=# select timestamp '2019-07-09 10:00:00.000000+00' - timestamptz '2019-07-09 10:00:00.000000+00' AS "Time Offset";
 Time Offset 
-------------
 00:00:00
(1 row)

postgres=# select timestamp '2019-07-09 10:00:00.000000+00' - timestamptz '2019-07-09 10:00:00.000000EST' AS "Time Offset";
 Time Offset 
-------------
 -05:00:00
(1 row)

postgres=# select timestamp '2019-07-09 10:00:00.000000+00' - timestamptz '2019-07-08 10:00:00.000000EST' AS "Time Offset";
 Time Offset 
-------------
 19:00:00
(1 row)

postgres=# select timestamp '2019-07-09 10:00:00.000000+00' - timestamptz '2019-07-07 10:00:00.000000EST' AS "Time Offset";
  Time Offset   
----------------
 1 day 19:00:00
(1 row)

postgres=# select age(timestamp '2019-07-09 10:00:00.000000+00', timestamptz '2019-07-07 10:00:00.000000EST') AS "Age Diff";
    Age Diff   
----------------
 1 day 19:00:00
(1 row)

postgres=# select (extract('days' from age(timestamp '2019-07-09 10:00:00.000000+00', timestamptz '2019-07-07 10:00:00.000000EST'))*24)+
           (extract('hours' from age(timestamp '2019-07-09 10:00:00.000000+00', timestamptz '2019-07-07 10:00:00.000000EST'))) AS "Hours Diff";
           
 Hours Diff 
------------
         43

```

## Manipulating Dates and Times
Note that the `EXTRACT` command is the preferred command to `DATE_PART`. Another useful command is `DATE_TRUNC` which is used to 'floor' the timestamp to a particular unit. 

```sql
yb_demo=# select date_trunc('hour', current_timestamp);
       date_trunc       
------------------------
 2019-07-09 06:00:00+00
(1 row)

yb_demo=# select to_char((date_trunc('month', generate_series)::date)-1, 'DD-MON-YYYY') AS "Last Day of Month" 
          from generate_series(current_date-364, current_date, '1 month');
          
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

yb_demo=# select date_trunc('days', age(created_at)) AS "Product Age" from products order by 1 desc limit 10;

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

Casts can be done for time(tz), date and timestamp(tz) like `raw::timestamptz`. 

## Conclusion
As illustrated, the area of dates and times is a comprehensive area that is well addressed by PostgreSQL and hence YSQL within YugaByte. All of the date time data types are implemented, and the vast majority of methods, operators and special values are available. The functionality is complex enough for you to be able to code any shortfalls that you find within the YSQL implementation of its SQL API. 
