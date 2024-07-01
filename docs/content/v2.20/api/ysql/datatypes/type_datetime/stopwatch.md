---
title: >
  Case study: implementing a stopwatch with SQL [YSQL]
headerTitle: >
  Case study: implementing a stopwatch with SQL
linkTitle: >
  Case study: SQL stopwatch
description: >
  Case study: using YSQL to implement a stopwatch
menu:
  v2.20:
    identifier: stopwatch
    parent: api-ysql-datatypes-datetime
    weight: 130
type: docs
---

You sometimes want to time a sequence of several SQL statements, issued from _ysqlsh_, and to record the time in the spool file. The \\_timing on_ meta-command doesn't help here because it reports the time after every individual statement and, on Unix-like operating systems, does this using _stderr_. The \\_o_ meta-command doesn't redirect _stderr_ to the spool file. This case study shows you how to implement a SQL stopwatch that allows you to start it with a procedure call before starting what you want to time and to read it with a _select_ statement when what you want to time finishes. This reading goes to the spool file along with all other _select_ results.

## How to read the wall-clock time and calculate the duration of interest

There are various built-in SQL functions for reading the wall-clock time because there are different notions of currency: "right now at the instant of reading , independently of statements and transactions", "as of the start of the current individual SQL statement", or "as of the start of the current transaction". A generic elapsed time stopwatch is best served by reading the wall-clock as it stands at the instant of reading, even as time flows on while a SQL statement executes. The _clock_timestamp()_ function does just this. Try this:

```plpgsql
set timezone = 'UTC';
select
  to_char(clock_timestamp(), 'hh24:mi:ss') as t0,
  (pg_sleep(5)::text = '')::text as slept,
  to_char(clock_timestamp(), 'hh24:mi:ss') as t1;
```

Here's a typical result:

```output
    t0    | slept |    t1
----------+-------+----------
 20:13:54 | true  | 20:13:59
```

(The _pg_sleep()_ built-in function returns _void_, and the _::text_ typecast of _void_ is the empty string.) You can see that _t1_ is five seconds later than _t0_, just as the invocation of _pg_sleep()_ requests.

The return data type of _clock_timestamp()_ is _timestamptz_. (You can confirm this with the \\_df_ meta-command.) Try this.

```plpgsql
select
  to_char((clock_timestamp() at time zone 'America/Los_Angeles'), 'yyyy-mm-dd hh24:mi:ss') as "LAX",
  to_char((clock_timestamp() at time zone 'Europe/Helsinki'),     'yyyy-mm-dd hh24:mi:ss') as "HEL";
```

Notice the use of the _[at time zone](../timezones/syntax-contexts-to-spec-offset/#specify-the-utc-offset-using-the-at-time-zone-operator)_ operator to interpret a _timestamptz_ value as a plain _timestamp_ value local to the specified timezone. In this use of the operator (converting from _timestamptz_ to plain _timestamp_), its effect is insensitive to the session's _TimeZone_ setting. Here's a typical result:

```output
         LAX         |         HEL
---------------------+---------------------
 2021-07-30 10:26:39 | 2021-07-30 20:26:39
```

The reported local time in Helsinki is ten hours later than the reported local time in Los Angeles, as is expected around the middle of the summer.

You might be worried by the fact that the _text_ rendition of the _clock_timestamp()_ return value depends on the session's _TimeZone_ setting. And you might think that this could confound the attempt to measure elapsed time by subtracting the reading at the start from the reading at the finish of the to-be-timed statements unless care is taken to ensure that the _TimeZone_ setting is identical at the start and the end of the timed duration. The simplest way to side-step this concern is to record the value of this expression at the start and at the finish:

```output
extract(epoch from clock_timestamp())
```

This evaluates to the number of seconds (as a _double precision_ value with microsecond precision) of the specified moment from the so-called start of the epoch (_00:00:00_ on _1-Jan-1970 UTC_). The [plain _timestamp_ and _timestamptz_ data types](../date-time-data-types-semantics/type-timestamp/) section explains, and demonstrates, that the result of _"extract(epoch from timestamptz_value)"_ is insensitive to the session's _TimeZone_ setting. Try this, using the LAX and HEL results from the previous query as the _timestamptz_ literals.

```plpgsql
with c as (
  select
    '2021-07-30 10:26:39 America/Los_Angeles'::timestamptz as t1,
    '2021-07-30 20:26:39 Europe/Helsinki'    ::timestamptz as t2
    )
select (
    extract(epoch from t1) =
    extract(epoch from t2)
  )::text as same
from c;
```

The result is _true_. This reflects the fact that _extract()_ accesses the internal representation of the _timestamptz_ value and this is normalized to UTC when it's recorded.

Notice that, if you insist, you could subtract the start _timestamptz_ value directly from the finish _timestamptz_ value to produce an _interval_ value. But you’d have to reason rather more carefully to prove that your timing results are unaffected by the session’s timezone setting—particularly in the rare, but perfectly possible, event that a daylight savings boundary is crossed, in the regime of the session’s _TimeZone_ setting, while the to-be-timed operations execute. (Reasoning does show that these tricky details don’t affect the outcome. But it would be unkind to burden readers of your code with needing to understand this when a more obviously correct approach is available.) Further, the _duration_as_text()_ user-defined function (below) to format the result using appropriate units would be a little bit harder to write when you start with an _interval_ value than when you start with a scalar seconds value.

## How to note the wall-clock time so that you can read it back after several SQL statements have completed

PostgreSQL doesn't provide any construct to encapsulate PL/pgSQL functions and procedures and to represent state that these can jointly set and read that persists across the boundaries of invocations of these subprograms. YSQL inherits this limitation. (Other database programming environments do provide such schemes. For example, Oracle Database's PL/SQL provides the _package_ for this purpose.) However, PostgreSQL and YSQL do support the so-called user-defined run-time parameter. This supports the representation of session-duration, session-private, state as a _text_ value. Here's a simple example:

```plpgsql
-- Record a value.
set my_namespace.my_value to 'Some text.';

-- Read it back later.
select current_setting('my_namespace.my_value') as "current value of 'my_namespace.my_value'";
```

This is the result:

```output
 current value of 'my_namespace.my_value'
------------------------------------------
 Some text.
```

You don't have to declare the namespace or items within it. They come in to being implicitly when you first write to them. Notice that the effect of the _set_ statement is governed by ordinary transaction semantics. Try this (with _autocommit_ set to _on_, following the usual convention):

```plpgsql
set my_namespace.my_value to 'First value.';

begin;
set my_namespace.my_value to 'Second value.';
rollback;

select current_setting('my_namespace.my_value') as "current value of 'my_namespace.my_value'";
```

This is the result:

```output
 current value of 'my_namespace.my_value'
------------------------------------------
 First value.
```

## The duration_as_text() formatting function

The implementation and testing of this function are utterly straightforward—but a fair amount of typing is needed. The code presented here can save you that effort. You may prefer to use it as a model for implementing your own formatting rules.

### Create function duration_as_text()

The return data type of the _extract()_ operator is _double precision_. But it’s better to implement _duration_as_text()_ with a _numeric_ input formal parameter because of the greater precision and accuracy of this data type. This makes behavior with input values that are very close to the units boundaries that the inequality tests define more accurate than if _double precision_ is used. Of course, this doesn’t matter when the function is used for its ultimate purpose because ordinary stochastic timing variability will drown any concerns about the accuracy of the formatting. But testing is helped when results with synthetic data agree reliably with what you expect.

As is often the case with formatting code, the readability benefits from trivial encapsulations of the SQL built-in functions that it uses—in this case _to_char()_ and _ltrim()_. First create this overload-pair of helpers:

```plpgsql
drop function if exists fmt(numeric, text) cascade;

create function fmt(n in numeric, template in text)
  returns text
  stable
  language plpgsql
as $body$
begin
  return ltrim(to_char(n, template));
end;
$body$;

drop function if exists fmt(int, text) cascade;

create function fmt(i in int, template in text)
  returns text
  stable
  language plpgsql
as $body$
begin
  return ltrim(to_char(i, template));
end;
$body$;
```

Now create _duration_as_text()_ itself:

```plpgsql
drop function if exists duration_as_text(numeric) cascade;

create function duration_as_text(t in numeric)
  returns text
  stable
  language plpgsql
as $body$
declare
  ms_pr_sec         constant numeric not null := 1000.0;
  secs_pr_min       constant numeric not null := 60.0;
  mins_pr_hour      constant numeric not null := 60.0;
  secs_pr_hour      constant numeric not null := mins_pr_hour*secs_pr_min;
  secs_pr_day       constant numeric not null := 24.0*secs_pr_hour;

  confidence_limit  constant numeric not null := 0.02;
  ms_limit          constant numeric not null := 5.0;
  cs_limit          constant numeric not null := 10.0;

  result                     text    not null := '';
begin
  case
    when t < confidence_limit then
      result := 'less than ~20 ms';

    when t >= confidence_limit and t < ms_limit then
      result := fmt(t*ms_pr_sec, '9999')||' ms';

    when t >= ms_limit and t < cs_limit then
      result := fmt(t, '90.99')||' ss';

    when t >= cs_limit and t < secs_pr_min then
      result := fmt(t, '99.9')||' ss';

    when t >= secs_pr_min and t < secs_pr_hour then
      declare
        ss   constant numeric not null := round(t);
        mins constant int     not null := trunc(ss/secs_pr_min);
        secs constant int     not null := ss - mins*secs_pr_min;
      begin
        result := fmt(mins, '09')||':'||fmt(secs, '09')||' mi:ss';
      end;

    when t >= secs_pr_hour and t < secs_pr_day then
      declare
        mi    constant numeric not null := round(t/secs_pr_min);
        hours constant int     not null := trunc(mi/mins_pr_hour);
        mins  constant int     not null := round(mi - hours*mins_pr_hour);
      begin
        result := fmt(hours, '09')||':'||fmt(mins,  '09')||' hh:mi';
      end;

    when t >= secs_pr_day then
      declare
        days  constant int     not null := trunc(t/secs_pr_day);
        mi    constant numeric not null := (t - days*secs_pr_day)/secs_pr_min;
        hours constant int     not null := trunc(mi/mins_pr_hour);
        mins  constant int     not null := round(mi - hours*mins_pr_hour);
      begin
        result := fmt(days,  '99')||' days '||
                  fmt(hours, '09')||':'||fmt(mins,  '09')||' hh:mi';
      end;
  end case;
  return result;
end;
$body$;
```

### Create function duration_as_text_test_report()

It’s convenient to encapsulate the tests for the function _duration_as_text()_ using a table function to generate a nicely formatted report. The readability of this reporting function, too, benefits from a dedicated helper function along the same lines as the helpers for the _duration_as_text()_ function.

```plpgsql
drop function if exists display(numeric) cascade;
create function display(n in numeric)
  returns text
  stable
  language plpgsql
as $body$
begin
  return lpad(to_char(n, '999990.999999'), 15)||' :: '||duration_as_text(n);
end;
$body$;
```

Now create and execute the _duration_as_text_test_report()_ table function itself:

```plpgsql
drop function if exists duration_as_text_test_report() cascade;

create function duration_as_text_test_report()
  returns table (z text)
  stable
  language plpgsql
as $body$
declare
  secs_pr_min       constant numeric not null := 60.0;
  secs_pr_hour      constant numeric not null := 60.0*secs_pr_min;
  secs_pr_day       constant numeric not null := 24.0*secs_pr_hour;

  confidence_limit  constant numeric not null := 0.02;
  ms_limit          constant numeric not null := 5.0;
  cs_limit          constant numeric not null := 10.0;

  delta             constant numeric not null := 0.000001;
  cs                constant numeric not null := 0.1;
  sec               constant numeric not null := 1;
  min               constant numeric not null := secs_pr_min;

  t                          numeric not null := 0;
begin
  z := ' s (in seconds)   duration_as_text(s)';           return next;
  z := ' --------------   -------------------';           return next;

  t := confidence_limit - delta;     z := display(t);     return next;
                                     z := '';             return next;
  t := confidence_limit;             z := display(t);     return next;
  t := ms_limit - delta;             z := display(t);     return next;
                                     z := '';             return next;
  t := ms_limit;                     z := display(t);     return next;
  t := cs_limit - delta;             z := display(t);     return next;
                                     z := '';             return next;
  t := cs_limit;                     z := display(t);     return next;
  t := secs_pr_min - cs;             z := display(t);     return next;
  t := secs_pr_min - delta;          z := display(t);     return next;
                                     z := '';             return next;
  t := secs_pr_min;                  z := display(t);     return next;
  t := secs_pr_hour - sec;           z := display(t);     return next;
  t := secs_pr_hour - delta;         z := display(t);     return next;
                                     z := '';             return next;
  t := secs_pr_hour;                 z := display(t);     return next;
  t := secs_pr_day - min;            z := display(t);     return next;
  t := secs_pr_day - delta;          z := display(t);     return next;
                                     z := '';             return next;
  t := secs_pr_day;                  z := display(t);     return next;
  t := secs_pr_day*2.345;            z := display(t);     return next;
end;
$body$;

select z from duration_as_text_test_report();
```

This is the result:

```output
  s (in seconds)   duration_as_text(s)
  --------------   -------------------
        0.019999 :: less than ~20 ms

        0.020000 :: 20 ms
        4.999999 :: 5000 ms

        5.000000 :: 5.00 ss
        9.999999 :: 10.00 ss

       10.000000 :: 10.0 ss
       59.900000 :: 59.9 ss
       59.999999 :: 60.0 ss

       60.000000 :: 01:00 mi:ss
     3599.000000 :: 59:59 mi:ss
     3599.999999 :: 60:00 mi:ss

     3600.000000 :: 01:00 hh:mi
    86340.000000 :: 23:59 hh:mi
    86399.999999 :: 24:00 hh:mi

    86400.000000 :: 1 days 00:00 hh:mi
   202608.000000 :: 2 days 08:17 hh:mi
```

## PL/pgSQL encapsulations for starting and reading the stopwatch

Create a the procedure _start_stopwatch()_, the function _stopwatch_reading_as_dp()_, and the function _stopwatch_reading()_.

### Create procedure start_stopwatch()

This reads the wall-clock time and records it in a dedicated user-defined run-time parameter:

```plpgsql
drop procedure if exists start_stopwatch() cascade;

create procedure start_stopwatch()
  language plpgsql
as $body$
declare
  -- Record the current wall-clock time as (real) seconds
  -- since midnight on 1-Jan-1970.
  start_time constant text not null := extract(epoch from clock_timestamp())::text;
begin
  execute 'set stopwatch.start_time to '''||start_time||'''';
end;
$body$;
```

### Create function stopwatch_reading_as_dp()

This reads the current wall-clock time, reads the start time from the user-defined run-time parameter, and returns the elapsed time as a _double precision_ value:

```plpgsql
drop function if exists stopwatch_reading_as_dp() cascade;

create function stopwatch_reading_as_dp()
  returns double precision
  -- It's critical to use "volatile" because "clock_timestamp()" is volatile.
  -- "volatile" is the default. Spelled out here for self-doc.
  volatile
  language plpgsql
as $body$
declare
  start_time  constant double precision not null := current_setting('stopwatch.start_time');
  curr_time   constant double precision not null := extract(epoch from clock_timestamp());
  diff        constant double precision not null := curr_time - start_time;
begin
  return diff;
end;
$body$;
```

### Create function stopwatch_reading()

This is a simple wrapper for _stopwatch_reading_as_dp()_ that returns the elapsed time since the stopwatch was started as a _text_ value by applying the function _duration_as_text()_ to the _stopwatch_reading_as_dp()_ return value:

```plpgsql
drop function if exists stopwatch_reading() cascade;

create function stopwatch_reading()
  returns text
  -- It's critical to use "volatile" because "stopwatch_reading_as_dp()" is volatile.
  volatile
  language plpgsql
as $body$
declare
  t constant text not null := duration_as_text(stopwatch_reading_as_dp()::numeric);
begin
  return t;
end;
$body$;
```

### End-to-end test

The first execution of a PL/pgSQL unit in a session can be slower than subsequent executions because of the cost of compiling it. In a timing test like this, it therefore makes sense deliberately to warm up before recording the timings.

```plpgsql
\timing off
-- Warm up
do $body$
begin
  for j in 1..3 loop
    call start_stopwatch();
    perform pg_sleep(0.1);
    declare
      t constant text not null := stopwatch_reading();
    begin
    end;
  end loop;
end;
$body$;

call start_stopwatch();
\timing on
do $body$
begin
  perform pg_sleep(6.78);
end;
$body$;
\timing off

\o test-stopwatch.txt
select stopwatch_reading();
\o
```

Here is a typical result, as reported in the file _stopwatch.txt_:

```output
 stopwatch_reading
-------------------
 6.79 ss
```

As explained, the _"\timing on"_ output isn't captured in the spool file. But here's what you typically see in the terminal window:

```output
Time: 6783.047 ms (00:06.783)
```

The elapsed times that _"\timing on"_ and the SQL stopwatch report are in good agreement with each other. And both are a little longer that the argument that was used for _pg_sleep()_ because of the times for the client-server round trips.

## What to do when the to-be-timed operations are done in two or several successive sessions

Sometimes you want to time operations that are done in two or several successive sessions. Installation scripts do this when they need to create objects with two or several different owners. It won't work, in such scenarios, to record the starting wall-clock time using a user-defined run-time parameter because this has only session duration. Both _psql_ and (therefore) _ysqlsh_ support client-side variables—and these survive across session boundaries.

### Using the \gset and \set ysqlsh meta-commands

You can assign the result column(s) of a _select_ statement to such variables with the \\_gset_ meta-command. The _select_ statement isn't terminated by the usual semicolon. Rather, the \\_gset_ meta-command acts: _both_ as the directive to assign the value of the select list item _s0_ to the variable _stopwatch_s0_; _and_ as the terminator for the _select_ statement.

```plpgsql
select extract(epoch from clock_timestamp())::text as s0
\gset stopwatch_

-- See what the effect was.
\echo :stopwatch_s0
```

Create a new _double precision_ overload for the function _stopwatch_reading()_ to return the elapsed time like this:

```plpgsql
drop function if exists stopwatch_reading(double precision) cascade;

create function stopwatch_reading(start_time in double precision)
  returns text
  volatile
  language plpgsql
as $body$
declare
  curr_time   constant double precision not null := extract(epoch from clock_timestamp());
  diff        constant double precision not null := curr_time - start_time;
begin
  return duration_as_text(diff::numeric);
end;
$body$;
```

Test it like this:

```plpgsql
select stopwatch_reading(:stopwatch_s0);
```

You'll get a result that reflects the time that it took you to read the few sentences above and to copy-and-paste the code.

It's convenient to assign the SQL statements that start and read the stopwatch to variables so that you can then use these as shortcut commands, to save typing:

```plpgsql
\set start_stopwatch 'select extract(epoch from clock_timestamp())::text as s0 \\gset stopwatch_'
```

and:

```plpgsql
\set stopwatch_reading 'select stopwatch_reading(:stopwatch_s0);'
```

You can define these shortcuts in the _psqlrc_ file (on the _postgres/etc_ directory under the directory where you've installed the YugabyteDB client code).

{{< tip title="'ysqlsh' meta-command syntax" >}}
The section [ysqlsh](../../../../../admin/ysqlsh-meta-commands/) describes the \\_gset_ and the \\_set_ meta-commands.

(\\_gset_ is allowed on the same line as the SQL statement that it terminates, just as a new SQL statement is allowed on the same line as a previous SQL statement that's terminated with a semicolon.)

Such a client-side variable acts like a macro: the text that it stands for is eagerly substituted and only then is the command, as it now stands, processed in the normal way. The argument of the \\_set_ meta-command that follows the variable name needs to be surrounded with single quotes when it contains spaces that you intend to be respected. If you want to include a single quote within the argument, then you escape it with a backslash. And if you want to include a backslash within the argument, then you escape that with a backslash too. Try this:

```plpgsql
\set x 'select \'Hello \\world\' as v;'
\echo :x
```

This is the result:

```output
select 'Hello \world' as v;
```

Now try this:

```plpgsql
:x
```

This is the result:

```output
      v
--------------
 Hello \world
```

When you want to define a command as the variable _x_ and within that expand another variable _y_ for use within that command, do it like this:

```plpgsql
\set x 'select \'Hello \'||:y as v;'
\set y 42
:x
```

This is the result:

```output
    v
----------
 Hello 42
```

If you subsequently redefine the variable _y_ without redefining the variable _x_, and then use _x_, like this:

```plpgsql
\set y 17
:x
```

then you'll get this new result:

```output
    v
----------
 Hello 17
```

{{< /tip >}}

### End-to-end test

To be convincing, the end-to-end test needs to connect as at least two different users in the same database. Because session creation brings a noticeable cost, this drowns the first-use cost of invoking PL/pgSQL code—and so the test has no warm-up code.

The test assumes that two ordinary users, _u1_ and _u2_ exist in the database _demo_, that the stopwatch code is on the search path for both of these users, and that both of these have sufficient privileges to execute the code. Try this:

```plpgsql
\o test-cross-session-stopwatch.txt
\c demo u1
:start_stopwatch
do $body$
begin
  perform pg_sleep(2);
end;
$body$;

\c demo u2
do $body$
begin
  perform pg_sleep(3);
end;
$body$;

\c demo u1
do $body$
begin
  perform pg_sleep(1.5);
end;
$body$;
:stopwatch_reading
\o
```

The _test-cross-session-stopwatch.txt_ spool file will contain a result like this:

```output
 stopwatch_reading
-------------------
 6.86 ss
```

In this example, session creation cost explains the fact that the reported time, _6.86_ seconds, is longer than the total requested sleep time, _6.5_ seconds.

## Download the code kit

The code that this page explains isn't referenced elsewhere in the overall _[date-time](../../type_datetime/)_ section. You might like to install it in any database that you use for development and testing. (This is strongly encouraged.) For this reason, it's bundled for a one-touch installation, separately from the [downloadable date-time utilities code](../download-date-time-utilities/).

{{< tip title="Download the code kit" >}}
Get the code kit from [this download link](https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/sample/date-time-utilities/stopwatch.zip). It has a _README_ that points you to the master-install script and that recommends installing it centrally for use by any user of that database.
{{< /tip >}}
