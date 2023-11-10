---
title: Functions that return the current date-time moment [YSQL]
headerTitle: Functions that return the current date-time moment
linkTitle: Current date-time moment
description: The semantics of the functions that return the current date-time moment. [YSQL]
menu:
  v2.18:
    identifier: current-date-time-moment
    parent: date-time-functions
    weight: 40
type: docs
---

Each of the nine functions in this group returns a moment value of the specified data type that honors the "moment kind" semantics. (See the note "Don't use "_timeofday()_" above. For the reasons that it explains, that function is not included in the count of ten that the table in the subsection [Functions that return the current date-time moment](../#functions-that-return-the-current-date-time-moment-current-date-time-moment) presents. This is why the present paragraph starts with "Each of the _nine_ functions...")

Except for the variants _current_time(precision)_, _current_timestamp(precision)_, _localtime(precision)_, and _localtimestamp(precision)_, the functions have no formal parameters. The note [Functions without trailing parentheses](../#functions-without-trailing-parentheses), at the start of the parent page, lists these four together with _current_date_ (which has no optional _precision_ parameter) and calls out their exceptional status. There is no need, therefore, to document each "current _date-time_ moment" function individually.

The following anonymous PL/pgSQL block tests that the return data types are as expected.

```plpgsql
do $body$
begin
  assert
    -- date
        pg_typeof(current_date)            ::text = 'date'                        -- start of transaction

    -- plain time
    and pg_typeof(localtime)               ::text = 'time without time zone'      -- start of transaction

    -- timetz
    and pg_typeof(current_time)            ::text = 'time with time zone'         -- start of transaction

    -- plain timestamp
    and pg_typeof(localtimestamp)          ::text = 'timestamp without time zone' -- start of transaction

    -- timestamptz
    and pg_typeof(transaction_timestamp()) ::text = 'timestamp with time zone'    -- start of transaction
    and pg_typeof(now())                   ::text = 'timestamp with time zone'    -- start of transaction
    and pg_typeof(current_timestamp)       ::text = 'timestamp with time zone'    -- start of transaction
    and pg_typeof(statement_timestamp())   ::text = 'timestamp with time zone'    -- start of statement
    and pg_typeof(clock_timestamp())       ::text = 'timestamp with time zone'    -- instantaneous
    ,
  'assert failed';
end;
$body$;
```

It finishes without error showing that all the assertions hold.

There are five moment data types. (This number includes _timetz_. But the PostgreSQL documentation, and therefore the YSQL documentation, recommend against using this. See [this note](../../../type_datetime#avoid-timetz) on the "Date and time data types" major section's main page.) So, with three kinds of moment, you might expect every cell in the implied five-by-three matrix to be filled with a differently named function. However, there are only nine differently named functions (not counting _timeofday()_). This reflects two facts:

- Only the functions that return a _timestamptz_ value have "start of statement" and "instantaneous" variants.

- There are three differently named "start of transaction" functions that return a _timestamptz_ value: _transaction_timestamp()_, _now()_, and _current_timestamp_.

{{< tip title="Aim to use only 'transaction_timestamp()', 'statement_timestamp()', and 'clock_timestamp()' to get the current moment." >}}
Modern applications almost invariably must work globally—in other words, they must be timezone-aware. When, as is recommended, _timetz_ is avoided, the only timezone-aware moment data type is _timestamptz_. Therefore, when you need to get any kind of current moment, you must use a function that returns this data type. And the set _transaction_timestamp()_, _statement_timestamp()_, and _clock_timestamp()_ does indeed cover all three kinds of moment for this data type.

The functions _now()_ and _current_timestamp_ both have identical semantics to _transaction_timestamp()_. But the latter has the name that best expresses its purpose. Yugabyte therefore recommends that you avoid using _now()_ or _current_timestamp_.

Notice that, while _date_ might seem appealing because facts like "hire date" are conventionally recorded without a time of day component, different locations around the globe can have different dates at the same absolute moment. (See the section [Absolute time and the UTC Time Standard](../../conceptual-background/#absolute-time-and-the-utc-time-standard).) So it's better to record even "hire date" as a _timestamptz_ value. If you need to display this as a plain date, then you can convert it to a plain _timestamp_ value in the desired timezone (see the section [function timezone() | at time zone operator](../miscellaneous/#function-timezone-at-time-zone-operator-returns-time-timetz-timestamp-timestamptz)); and then you can typecast this value to a _date_ value (see the subsection [plain timestamp to date](../../typecasting-between-date-time-values/#plain-timestamp-to-date) in the section "Typecasting between values of different date-time datatypes").
{{< /tip >}}

### The semantics of "start of transaction", "start of statement", and "instantaneous"

- **"Start of transaction"** means literally this. The function _transaction_timestamp()_ implements this semantics. It returns the same value on successive invocations within the same transaction.

- **"Start of statement"** means literally this. The function _statement_timestamp()_ implements this semantics. It returns the same value on successive invocations only within the same SQL statement. Invocations in successive different SQL statements within the same transaction will report different values for each statement.

- **"Instantaneous"** means what a wall clock reads at the instant of invocation irrespective of progress during an explicitly started transaction and during individual SQL statement execution.  The function _clock_timestamp()_ implements this semantics.

Notice that applications typically run with "autocommit" set to "on". In the rare case that a single SQL statement is issued in this mode without surrounding transaction control calls, then invocations of _transaction_timestamp()_ and _statement_timestamp()_ will return the same value. Try this:

```plpgsql
-- self-doc
\set AUTOCOMMIT 'on'

select
  ''''''''||transaction_timestamp() ::text||''''''''||'::timestamptz' as txn,
  ''''''''||statement_timestamp()   ::text||''''''''||'::timestamptz' as stm
\gset r_

select
  (
    extract(epoch from :r_stm) =
    extract(epoch from :r_txn)
  )::text as "txn and stmt times are identical";
```

The result is _true_.

Usually, when an application makes only pure SQL calls (and no PL/pgSQL calls), business transaction atomicity is implemented like this:

- Execute the _set transaction_ statement before executing two or several data-changing calls.
- Then execute _commit_ (or _rollback_ if an error occurs) after these data-changing calls.

In this case, there is a clear distinction between the "start of transaction" moment and subsequent "start of statement" moments. Try this:

```plpgsql
-- self-doc
\set AUTOCOMMIT 'on'

start transaction;

\! sleep 5

select
  ''''''''||transaction_timestamp() ::text||''''''''||'::timestamptz' as txn,
  ''''''''||statement_timestamp()   ::text||''''''''||'::timestamptz' as stm
\gset r1_

\! sleep 5

select
  ''''''''||transaction_timestamp() ::text||''''''''||'::timestamptz' as txn,
  ''''''''||statement_timestamp()   ::text||''''''''||'::timestamptz' as stm
\gset r2_

commit;
```

Confirm that _transaction_timestamp()_ returns the same value throughout the transaction:

```plpgsql
select
  (
    extract(epoch from :r2_txn) =
    extract(epoch from :r2_txn)
  )::text as "txn times are identical";
```

The result is _true_.

Confirm that the first invocation of statement_timestamp() returns a value that's about _5 seconds_ (the sleep time) after the value that _transaction_timestamp()_ returns:

```plpgsql
select to_char(
  (
    extract(epoch from :r1_stm) -
    extract(epoch from :r1_txn)
  ),
  '9.999') as "stmt #1 time minus txn time";
```

You'll see that _"stmt #1 time minus txn time"_ differs from 5.0 seconds by maybe as much as 0.03 seconds. This reflects the inevitable time that it takes for the client-server round trips and for the \\_!_ meta-command to launch a new shell and invoke the operating system sleep utility. (_psql_ and therefore _ysqlsh_ have no intrinsic sleep function. And if you use _pg_sleep()_, then you complicate the discussion because this requires a top-level SQL call of its own.)

Notice that this observation confirms that it is the _start transaction_ explicit call that determines the moment at which the transaction starts—and not the start moment of the first subsequent database call within that started transaction.

Confirm that the second invocation of _statement_timestamp()_ returns a value that's about _10 seconds_ (the sum of the two sleep times) after the value that _transaction_timestamp()_ returns:

```plpgsql
select to_char(
  (
    extract(epoch from :r2_stm) -
    extract(epoch from :r1_txn)
  ),
  '99.999') as "stmt #2 time minus txn time";
```

You'll see _"stmt #2 time minus txn time"_ differs from _10.0 seconds_ by maybe as much as _0.03 seconds_.

Now that the difference in semantics between _transaction_timestamp()_ and _statement_timestamp()_ is clear, it's sufficient to demonstrate the difference in semantics between _statement_timestamp()_ and _clock_timestamp()_ within a single statement. Try this:

```plpgsql
-- self-doc
\set AUTOCOMMIT 'on'

select
  ''''''''||statement_timestamp()   ::text||''''''''||'::timestamptz' as stm_1,
  ''''''''||clock_timestamp()       ::text||''''''''||'::timestamptz' as clk_1,
  ''''''''||pg_sleep(5)             ::text||''''''''||'::timestamptz' as dummy,
  ''''''''||statement_timestamp()   ::text||''''''''||'::timestamptz' as stm_2,
  ''''''''||clock_timestamp()       ::text||''''''''||'::timestamptz' as clk_2
\gset r_

select
  (
    extract(epoch from :r_stm_1) =
    extract(epoch from :r_stm_1)
  )::text as "stm_1 time and stm_2 times are identical";
```

The result is _true_.

Now check how the values returned by _clock_timestamp()_ differ as the statement executes:

```plpgsql
select to_char(
  (
    extract(epoch from :r_clk_2) -
    extract(epoch from :r_clk_1)
  ),
  '9.999') as "clk_2 time minus clk_1 time";
```

<a name="avoid-constant-now"></a>You'll see that _"clk_2 time minus clk_1 time"_ differs from _5.0 seconds_ by maybe just a couple of milliseconds. The smaller noise discrepancies in this test than in the previous tests reflect the fact that all the measurements are made within the server-side execution of a single top-level call.

{{< tip title="Don't use the special manifest constant 'now'." >}}
And, by extension, don't use the related special manifest constants _'today'_, _'tomorrow'_, and _'yesterday'_. (See the section [Special date-time manifest constants](../../../type_datetime/#special-date-time-manifest-constants) for more discussion of the advisability of using these, and other, special _date-time_ manifest constants.)

Try this:

```plpgsql
select (now() = 'now'::timestamptz)::text as "now() = 'now'";
```

The result is _true_. Now try this:

```plpgsql
deallocate all;
prepare stmt as
select to_char(
  (
    extract(epoch from now()) -
    extract(epoch from 'now'::timestamptz)
  ),
  '99.999') as "now() [immediate] - 'now' [at prepare time]";

\! sleep 5
execute stmt;

\! sleep 5
execute stmt;
```

You'll see that _"now() [immediate] - 'now' [at prepare time]"_ differs from _5.0 seconds_, and then from _10.0 seconds_, by up to about a few centiseconds from the two successive _execute_ invocations. Of course, the name of the column alias was chosen to indicate what's going on. The expression _'now'::timestamptz_ means _"evaluate the function now() just once, at 'compile time', and record the return value as a constant"_. You can produce a similar dramatic demonstration effect like this:

```plpgsql
drop table if exists t cascade;
create table t(k int primary key, t_at_table_creation_time timestamptz default 'now', t_at_insert_time timestamptz default now());
\! sleep 5
insert into t(k) values(1);

select to_char(
  (
    extract(epoch from t_at_insert_time) -
    extract(epoch from t_at_table_creation_time)
  ),
  '99.999') as "t_at_insert_time - t_at_table_creation_time"
from t
where k = 1;
```

Once again, the spelling of the names is the clue to understanding. You might see, in this test, that _"t_at_insert_time - t_at_table_creation_time"_ differs from _5.0 seconds_ by as much as _0.1 seconds_ because the table creation process takes some time to complete after the value of _'now'::timestamptz_ is frozen.

It's difficult to see how the semantics of the expression _'now'::timestamptz_ could be useful. But if you understand the meaning of this expression and are convinced that your use case needs this, then you can decide to use it. But you should help readers of your code with a careful explanation of your purpose and how your code meets this goal.
{{< /tip >}}

### Consider user-defined functions rather than 'today', 'tomorrow', and 'yesterday'

(You can simply use the built-in function _now()_ rather than the special constant '_now'_.)

The PostgreSQL documentation defines the meanings of these three constants thus:

- _'today':_ midnight (00:00) today

- _'tomorrow':_ midnight (00:00) tomorrow

- _'yesterday':_ midnight (00:00) yesterday

Consider these alternatives for the special constants:

```plpgsql
-- 'today'
drop function if exists today() cascade;
create function today()
  returns timestamptz
  volatile
  language plpgsql
as $body$
begin
  return date_trunc('day', clock_timestamp());
end;
$body$;

-- 'tomorrow'
drop function if exists tomorrow() cascade;
create function tomorrow()
  returns timestamptz
  volatile
  language plpgsql
as $body$
begin
  return today() + make_interval(days=>1);
end;
$body$;

-- 'yesterday'
drop function if exists yesterday() cascade;
create function yesterday()
  returns timestamptz
  volatile
  language plpgsql
as $body$
begin
  return today() - make_interval(days=>1);
end;
$body$;

set timezone = 'UTC';
select
  today()     as "today()",
  tomorrow()  as "tomorrow()",
  yesterday() as "yesterday()";
```

Of course, the result will depend on when you do this. Here's the result when it was done at about 20:00 local time on 30-Sep-2021 in the _America/Los_Angeles_ timezone.

```output
        today()         |       tomorrow()       |      yesterday()
------------------------+------------------------+------------------------
 2021-10-01 00:00:00+00 | 2021-10-02 00:00:00+00 | 2021-09-30 00:00:00+00
```
