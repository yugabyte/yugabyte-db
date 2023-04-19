---
title: General-purpose date and time functions [YSQL]
headerTitle: General-purpose date and time functions
linkTitle: General-purpose functions
description: Describes the general-purpose date and time functions. [YSQL]
image: /images/section_icons/api/subsection.png
menu:
  v2.14:
    identifier: date-time-functions
    parent: api-ysql-datatypes-datetime
    weight: 90
type: indexpage
---

This page lists all of the general-purpose _date-time_ functions. They are classified into groups according to the purpose.

- [Creating date-time values](#functions-for-creating-date-time-values-creating-date-time-values)
- [Manipulating date-time values](#functions-for-manipulating-date-time-values-manipulating-date-time-values)
- [Current date-time moment](#functions-that-return-the-current-date-time-moment-current-date-time-moment)
- [Delaying execution](#functions-for-delaying-execution-delaying-execution)
- [Miscellaneous](#miscellaneous-functions-miscellaneous)

<a name="functions-without-trailing-parentheses"></a>Notice that the so-called _date-time_ formatting functions, like:

- _to_date()_ or _to_timestamp()_, that convert a _text_ value to a _date-time_ value

- and to _char()_, that converts a _date-time_ value to a _text_ value

are described in the dedicated [Date and time formatting functions](../formatting-functions/) section.

{{< note title="Functions without trailing parentheses" >}}
Normally in PostgreSQL, and therefore in YSQL, a function invocation must be written with trailing parentheses—even when the invocation doesn't specify any actual arguments. These five date-time functions are exceptions to that rule:

- _current_date_, _current_time_, _current_timestamp_, _localtime_, and _localtimestamp_.

Notice that the \\_df_ metacommand produces no output for each of these five functions.

Each of these is in the group [functions that return the current date-time moment](#functions-that-return-the-current-date-time-moment-current-date-time-moment). If you invoke one of these using empty trailing parentheses, then you get the generic _42601_ syntax error. Each of these five names is reserved in SQL. For example, if you try to create a table with a column whose name is one of these five (without trailing parentheses in this case, of course), then you get the same _42601_ error. Notice that within this set of five exceptional functions that must not be invoked with empty trailing parentheses, these four have a variant that has a single _precision_ parameter: _current_time(precision)_, _current_timestamp(precision)_, _localtime(precision)_, and _localtimestamp(precision)_. This specifies the precision of the seconds value. (This explains why _current_date_ has no _precision_ variant.)

All of the other _date-time_ functions that this page lists must be written with trailing parentheses—conforming to the norm for function invocation. (Without trailing parentheses, it is taken as a name for a column in a user-created table or for a variable in PL/pgSQL.

You should regard the exceptional status of the _current_date_, _current_time_, _current_timestamp_, _localtime_, and _localtimestamp_ _date-time_ functions simply as a quirk. There are other such quirky functions. See this note in the section [9.25. System Information Functions](https://www.postgresql.org/docs/11/functions-info.html) in the PostgreSQL documentation:

> _current_catalog_, _current_role_, _current_schema_, _current_user_, _session_user_, and _user_ have special syntactic status [in the SQL Standard]: they must be called without trailing parentheses. In PostgreSQL, parentheses can optionally be used with _current_schema_, but not with the others.
{{< /note >}}

The following tables list all of the general purpose _date_time_ built-in functions, classified by purpose.

## [Functions for creating date-time values](./creating-date-time-values)

|                                                                                                 | **return data type** |
| ----------------------------------------------------------------------------------------------- | -------------------- |
| [make_date()](./creating-date-time-values#function-make-date-returns-date)                      | date                 |
| [make_time()](./creating-date-time-values#function-make-time-returns-plain-time)                | (plain) time         |
| [make_timestamp()](./creating-date-time-values#function-make-timestamp-returns-plain-timestamp) | (plain) timestamp    |
| [make_timestamptz()](./creating-date-time-values#function-make-timestamptz-returns-timestamptz) | timestamptz          |
| [to_timestamp()](./creating-date-time-values#function-to-timestamp-returns-timestamptz)         | timestamptz          |
| [make_interval()](./creating-date-time-values#function-make-interval-returns-interval)          | interval             |

## [Functions for manipulating date-time values](./manipulating-date-time-values)

|                                                                                                                                                                  | **return data type**                       |
| ---------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------ |
| [date_trunc()](./manipulating-date-time-values#function-date-trunc-returns-plain-timestamp-timestamptz-interval)                                                 | plain timestamp \| timestamptz \| interval |
| [justify_days() \| justify_hours() \| justify_interval()](./manipulating-date-time-values#function-justify-days-justify-hours-justify-interval-returns-interval) | interval                                   |

## [Functions that return the current date-time moment](./current-date-time-moment)

There are several built-in SQL functions for returning the current date-time moment because there are different notions of currency:

- right now at the instant of reading, independently of statements and transactions;
- as of the start of the current individual SQL statement within an on-going transaction;
- as of the start of the current transaction.

|                                                                                     | **return data type** | **Moment kind**      |
| ----------------------------------------------------------------------------------- | -------------------- | -------------------- |
| [current_date](./current-date-time-moment)                                          | date                 | start of transaction |
| [localtime](./current-date-time-moment)                                             | time                 | start of transaction |
| [current_time](./current-date-time-moment)                                          | timetz               | start of transaction |
| [localtimestamp](./current-date-time-moment)                                        | plain timestamp      | start of transaction |
| [transaction_timestamp() \| now() \| current_timestamp](./current-date-time-moment) | timestamptz          | start of transaction |
| [statement_timestamp()](./current-date-time-moment)                                 | timestamptz          | start of statement   |
| [clock_timestamp()](./current-date-time-moment)                                     | timestamptz          | instantaneous        |
| [timeofday()](#avoid-timeofday)                                                     | text                 | instantaneous        |

Notice that _timeofday()_ has the identical effect to `to_char(clock_timestamp(),'Dy Mon dd hh24:mi:ss.us yyyy TZ')`. But notice that the use of plain _'Dy'_ and plain _'Mon'_, rather than _'TMDy'_ and _'TMMon'_, calls specifically for the English abbreviations—in other words, _timeofday()_ non-negotiably returns an English text value.

Try this:

```plpgsql
-- Because "fmt" uses the plain forms "Dy" and "Mon", the test is insensitve to the value of "lc_time".
-- Setting it here to Finnish simply emphasizes this point.
set lc_time = 'fi_FI';

set timezone = 'America/Los_Angeles';

drop procedure if exists assert_timeofday_semantics() cascade;
create procedure assert_timeofday_semantics()
  language plpgsql
as $body$
declare
  clk_1         timestamptz not null := clock_timestamp();
  clk_2         timestamptz not null := clk_1;

  tod_1         text        not null := '';
  tod_2         text        not null := '';
  dummy         text        not null := '';
  fmt constant  text        not null := 'Dy Mon dd hh24:mi:ss.us yyyy TZ';
begin
  select
    clock_timestamp(), timeofday(), pg_sleep(2), clock_timestamp(), timeofday() into
    clk_1,             tod_1,       dummy,       clk_2,             tod_2;

  assert tod_1 = to_char(clk_1, fmt), 'Assert #1 failed';
  assert tod_2 = to_char(clk_2, fmt), 'Assert #2 failed';
end;
$body$;

call assert_timeofday_semantics();
```

<a name="avoid-timeofday"></a>Presumably, because it takes time to execute each individual PL/pgSQL statement, the moment values returned by the first calls to _clock_timestamp()_ and _timeofday()_, and then by the second calls to these two functions, will not be pairwise identical. However, they are the same to within a one microsecond precision. This is fortunate because it does away with the need to implement a tolerance notion and therefore simplifies the design of the test.

{{< tip title="Don't use 'timeofday()'." >}}
Using _clock_timestamp()_, and formatting the result to _text_, can bring the identical result to using _timeofday()_—if this meets your requirement. However, you might well want a different formatting notion and might want to render day and month names or abbreviations in a language other than English. Moreover, you might want to do arithmetic with the moment value, for example by subtracting it from some other moment value. Yugabyte recommends, therefore, that you simply avoid ever using _timeofday()_ and, rather, always start with _clock_timestamp()_.

For this reason, this section won't say any more about the _timeofday()_ builtin function.
{{< /tip >}}

## [Functions for delaying execution](./delaying-execution)

|                                                                               | **return data type** |
| ----------------------------------------------------------------------------- | -------------------- |
| [pg_sleep()](./delaying-execution#function-pg-sleep-returns-void)             | void                 |
| [pg_sleep_for()](./delaying-execution#function-pg-sleep-for-returns-void)     | void                 |
| [pg_sleep_until()](./delaying-execution#function-pg-sleep-until-returns-void) | void                 |

## [Miscellaneous functions](./miscellaneous/)

|                                                                                                                              | **return data type**                       |
| ---------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------ |
| [isfinite()](./miscellaneous#function-isfinite-returns-boolean)                                                              | boolean                                    |
| [age()](./miscellaneous#function-age-returns-interval)                                                                       | interval                                   |
| [extract() \| date_part()](./miscellaneous#function-extract-function-date-part-returns-double-precision)                     | double-precision                           |
| [timezone() \| at time zone operator](./miscellaneous#function-timezone-at-time-zone-operator-returns-timestamp-timestamptz) | time \| timetz \| timestamp \| timestamptz |
| [overlaps operator](./miscellaneous#overlaps-operator-returns-boolean)                                                       | boolean                                    |
