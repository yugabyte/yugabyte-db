---
title: Function age() returns integer [YSQL]
headerTitle: Function age() returns integer
linkTitle: Function age()
description: The semantics of "function age() returns integer". [YSQL]
menu:
  stable:
    identifier: age
    parent: miscellaneous
    weight: 10
type: docs
---

## The semantics of the two-parameter overload of function age(timestamp[tz], timestamp[tz])

This section defines the semantics of the overload of the function _age()_ with two parameters of data type plain _timestamp_ by implementing the defining rules in PL/pgSQL in the function _modeled_age()_. The rules by which the returned _interval_ value is calculated are the same for the _(timestamptz, timestamptz)_ overload as for the plain _(timestamp, timestamp)_ overload except that, for the _with time zone_ overload, the actual timezone component (whether this is specified implicitly or taken from the session environment) and the sensitivity to the reigning timezone have their usual effect.

### function days_pr_month() returns int

It turns out that to match the behaviour of the built-in _age(timestamp, timestamp)_ function, _modeled_age(timestamp, timestamp)_ needs to map a "borrowed" month into a month-specific number of days. (The subsection [The semantics of the built-in function age()](../#the-semantics-of-the-built-in-function-age) on this page's parent page introduces the notion of "borrowing".) The function _days_pr_month()_ implements the mapping. Because of the effect of leap years, the function needs both _year_ and _month_ parameters. Create the helper function thus:

```plpgsql
drop function if exists days_pr_month(int, int) cascade;

create function days_pr_month(year in int, month in int)
  returns int
  language plpgsql
as $body$
begin
  -- Self-doc. The value of "month" comes from "extract(month from ...)".
  assert (month between 1 and 12), 'days_pr_month: assert failed';

  declare
    m_next  constant int not null := case month
                                       when 12 then 1
                                       else         month + 1
                                     end;

    y_next  constant int not null := case m_next
                                       when 1 then year + 1
                                       else        year
                                     end;
  begin
    -- February needs special treatment 'cos of leap year possibility.
    -- So may as well use the same method for all months.
    return make_date(y_next, m_next, 1) - make_date(year, month, 1);
  end;
end;
$body$;
```

You can test it with examples like this:

```plpgsql
select
  days_pr_month(2011,  2) as "Feb-2011",
  days_pr_month(2012,  2) as "Feb-2012",
  days_pr_month(2013,  4) as "Apr-2013",
  days_pr_month(2016, 12) as "Dec-2016";
```

This is the result:

```output
 Feb-2011 | Feb-2012 | Apr-2013 | Dec-2016
----------+----------+----------+----------
       28 |       29 |       30 |       31
```

### function modeled_age(timestamp, timestamp) returns interval

Because the [PostgreSQL documentation](https://www.postgresql.org/docs/11/functions-datetime.html#FUNCTIONS-DATETIME-TABLE) says nothing of value to describe the semantics that the function _age(timestamp, timestamp)_ implements, the function _modeled_age(timestamp, timestamp)_ was developed to a large extent by trial and error—in other words, thorough testing with a huge set of input values was key. (See the subsections that describe the functions [modeled_age_vs_age()](#function-modeled-age-vs-age-timestamp-timestamp-returns-text) and [random_test_report_for_modeled_age()](#function-random-test-report-for-modeled-age) below.)

The implementation was made simpler by this intuitive, and trivially observable, realization:

```output
age(t1, t2) == -age (t2, t1)
```

The following intuition (copied from this page's parent page) is key to the scheme:

> The function _age()_ extracts the _year_, _month_, _day_, and _seconds_ since midnight for each of the two input moment values. It then subtracts these values pairwise and uses them to create an _interval_ value. But the statement of the semantics must be made more carefully than this to accommodate the fact that the outcomes of the pairwise differences might be negative.

So the first step, done conveniently in the _declare_ section, is to calculate the pairwise differences _months_, _days_, and _secs_ naïvely—accepting that the values might come out negative. However, because the input _timestamp_ values are exchanged, if necessary, so that the local variable _t_dob_ (for date-of-birth) is less than or equal to the local variable _t_today_., it's guaranteed that the naïvely computed value of the _years_ pairwise difference cannot be negative. There is, though, a different quirk to accommodate in the _years_ calculation: the year _zero_ (as a matter of arbitrary, but universal, convention) simply does not exist. This means that the _years_ difference between _1 AD_ and _1 BC_ is just _one_. This explains the use of the _case_ expression in the expression for _years_.

The executable block statement (from _begin_ through its matching _end;_) revisits the computation of each of _secs_, _days_, and _months_, in that order, by implementing "borrowing" from the immediately next coarser-grained difference value when the initially computed present value is negative. This is where the conversion from one "borrowed" month to a number of days must accommodate the number of days in the "borrowed" month—given not only which month it is but also, because of the possibility that this is February in a leap year, which year it is. This pinpoints a key question:

- Which is the "borrowed" month? Is it (a) the one with _[yy_dob, mm_dob]_; or (b) the one with _[yy_today, mm_today]_; or (c) maybe the month that precedes or follows either of these?

Empirical testing shows that the "borrowed" month is given by choice (a).

The candidate _interval_ value to return is then evaluated (using the revised values) as:

```output
make_interval(years=>years, months=>months, days=>days, secs=>secs);
```

The final step is simply to take account of whether the initial test showed that the input _timestamp_ values should be exchanged. If there was no exchange, then the candidate _interval_ value is returned "as is". But if the input _timestamp_ values were exchanged, then the _negated_ candidate _interval_ value is returned.

Create the function thus:

```plpgsql
drop function if exists modeled_age(timestamp, timestamp) cascade;

create function modeled_age(t_today_in in timestamp, t_dob_in in timestamp)
  returns interval
  language plpgsql
as $body$
declare
  -- Exchange the inputs for negative age.
  negative_age   constant boolean          not null := (t_today_in - t_dob_in) < make_interval();

  t_today        constant timestamp        not null := case negative_age
                                                         when true then t_dob_in
                                                         else           t_today_in
                                                       end;

  t_dob           constant timestamp       not null := case negative_age
                                                         when true then t_today_in
                                                         else           t_dob_in
                                                       end;

  secs_pr_day    constant double precision not null := 24*60*60;
  mons_pr_year   constant int              not null := 12;

  yy_today       constant int              not null := extract(year  from t_today);
  mm_today       constant int              not null := extract(month from t_today);
  dd_today       constant int              not null := extract(day   from t_today);
  ss_today       constant double precision not null := extract(epoch from t_today::time);

  yy_dob         constant int              not null := extract(year  from t_dob);
  mm_dob         constant int              not null := extract(month from t_dob);
  dd_dob         constant int              not null := extract(day   from t_dob);
  ss_dob         constant double precision not null := extract(epoch from t_dob::time);

  years                   int              not null :=
    case
      -- Special treatment is needed when yy_today and yy_dob span AC/BC
      -- 'cos there's no year zero.
      when yy_today > 0 and yy_dob < 0 then yy_today - yy_dob - 1
      else                                  yy_today - yy_dob
    end;

  months                  int              not null := mm_today - mm_dob;
  days                    int              not null := dd_today - dd_dob;
  secs                    double precision not null := ss_today - ss_dob;
begin
  if secs < 0 then
    secs := secs + secs_pr_day;
    days := days - 1;
  end if;

  if days < 0 then
    days := days + days_pr_month(yy_dob, mm_dob);
    months := months - 1;
  end if;

  if months < 0 then
    months := months + 12;
    years := years - 1;
  end if;

  declare
    age constant interval not null := make_interval(years=>years, months=>months, days=>days, secs=>secs);
  begin
    return case negative_age
      when true then -age
      else            age
    end;
  end;
end;
$body$;
```

### Function modeled_age_vs_age(timestamp, timestamp) returns text

The design of this is straightforward. It evaluates both _age()_ and _modeled_age()_ using the actual input _timestamp_ values. Then it compares them for equality. Notice that it's critical to use the user-defined "strict equals" operator, `==`, for a pair of _interval_ values rather than the native equals for this argument pair.

- The section [User-defined _interval_ utility functions](../../../date-time-data-types-semantics/type-interval/interval-utilities/#the-user-defined-strict-equals-interval-interval-operator) presents the code that creates the "strict equals" operator. The recommendation, given at the start of the section, is to download the _'.zip'_ file to create the reusable code that supports the pedagogy of the overall _date-time_ major section and then to execute the kit's "one-click" install script.

- The section [Comparing two _interval_ values](../../../date-time-data-types-semantics/type-interval/interval-arithmetic/interval-interval-comparison/) explains why you must use the "strict equals" operator.

The function returns a _text_ value that starts with the _text_ typecasts of the actual input _timestamp_ values followed by the _text_ typecast of the result given by the built-in _age()_ function. Only if the function _modeled_age_vs_age()_ gives a result that is not strictly equal to the built-in _age()_ function's result, is the modeled result appended to the returned _text_ value. The idea here is to reduce the visual noise in the output to make it easy to spot when (at least while _modeled_age()_ was under development) the results from the built-in and the modeled functions disagree.

Create the comparison function thus:

```plpgsql
drop function if exists modeled_age_vs_age(timestamp, timestamp) cascade;

create function modeled_age_vs_age(t_today in timestamp, t_dob in timestamp)
  returns text
  language plpgsql
as $body$
declare
  input constant text not null := lpad(t_today::text, 31)||' '||lpad(t_dob::text, 31)||' ';
  m constant interval not null := modeled_age(t_today, t_dob);
  a constant interval not null :=         age(t_today, t_dob);
begin
  return
    case (m == a)
      when true then input||lpad(a::text, 42)
      else           input||lpad(a::text, 42)||' ! '||lpad(m::text, 42)
    end;
end;
$body$;
```

Now exercise the comparison function with a few manually composed input values. The results are easiest to read if you use a table function encapsulation:

```plpgsql
drop function if exists manual_test_report_for_modeled_age() cascade;

create function manual_test_report_for_modeled_age()
  returns table(z text)
  language plpgsql
as $body$
begin
  z := lpad('t_today', 31     )||' '||lpad('t_dob', 31     )||' '||lpad('age()', 42     );    return next;
  z := lpad('-',       31, '-')||' '||lpad('-',     31, '-')||' '||lpad('-',     42, '-');    return next;

  -- Sanity test: zero age.
  z := modeled_age_vs_age('2019-12-21',                    '2019-12-21'                   );  return next;

  -- Positive ages.
  z := modeled_age_vs_age('2001-04-10',                    '1957-06-13'                   );  return next;
  z := modeled_age_vs_age('2001-04-10 11:19:17',           '1957-06-13 15:31:42'          );  return next;
  z := modeled_age_vs_age('0007-06-13 15:31:42.123456 BC', '2001-04-10 11:19:17.654321 BC');  return next;

  -- Negative age.
  z := modeled_age_vs_age('1957-06-13 15:31:42',        '2001-04-10 11:19:17'             );  return next;

  -- t_today and t_dob span the BC/AD transition.
  z := modeled_age_vs_age('0001-01-01 11:19:17',        '0001-01-01 15:31:42 BC'          );   return next;
  z := modeled_age_vs_age('0001-01-01 15:31:42 BC',     '0001-01-01 11:19:17'             );   return next;
end;
$body$;

select z from manual_test_report_for_modeled_age();
```

This is the result:

```output
                         t_today                           t_dob                                      age()
 ------------------------------- ------------------------------- ------------------------------------------
             2019-12-21 00:00:00             2019-12-21 00:00:00                                   00:00:00
             2001-04-10 00:00:00             1957-06-13 00:00:00                    43 years 9 mons 27 days
             2001-04-10 11:19:17             1957-06-13 15:31:42           43 years 9 mons 26 days 19:47:35
   0007-06-13 15:31:42.123456 BC   2001-04-10 11:19:17.654321 BC   1994 years 2 mons 3 days 04:12:24.469135
             1957-06-13 15:31:42             2001-04-10 11:19:17       -43 years -9 mons -26 days -19:47:35
             0001-01-01 11:19:17          0001-01-01 15:31:42 BC                   11 mons 30 days 19:47:35
          0001-01-01 15:31:42 BC             0001-01-01 11:19:17                -11 mons -30 days -19:47:35
```

There is no fourth results column—in other words, the comparison of the result from the user-defined _modeled_age_vs_age(timestamp, timestamp)_ and the result from the built-in _age()_ passes the [strict equals](../../../date-time-data-types-semantics/type-interval/interval-utilities/#the-user-defined-strict-equals-interval-interval-operator) test for each tested pair of inputs.

Notice that the first two "Positive ages" tests use the same values as do the examples on this page's parent page in the section [The semantics of the built-in function _age()_](../#the-semantics-of-the-built-in-function-age)

### Function random_timestamp() returns timestamp

Because the implementation of _modeled_age(timestamp, timestamp)_ function was designed using intuition and iterative refinement in response to trial-and-error, it's critically important to test the comparison of the result from this and the result from the built-in _age()_ function with a huge number of distinct input pairs. The only way to do this is to generate these pairs randomly. There is no available suitable random-number generator. But the function _gen_random_bytes()_ comes to the rescue. This is not, strictly speaking, a built-in. Rather, it comes when you install the _[pgcrypto](../../../../../../../explore/ysql-language-features/pg-extensions/#pgcrypto-example)_ extension.

{{< tip title="Always make the 'pgcrypto' extension centrally available in every database." >}}
Not only does installing the [_pgcrypto_](../../../../../../../explore/ysql-language-features/pg-extensions/#pgcrypto-example) extension bring the function _gen_random_bytes()_; also, it brings _gen_random_uuid()_. This is commonly used to populate a surrogate primary key column. Yugabyte therefore recommends that you adopt the practice routinely to install _pgcrypto_ (this must be done by a _superuser_) in a central "utilities" schema in every database that you create. By granting appropriate privileges and by including this schema in, for example, the second position in every regular user's search path, you can make _gen_random_bytes()_, _gen_random_uuid()_, and all sorts of other useful utilities immediately available to all users with no further fuss.

You might also like to install the _[tablefunc](../../../../../../../explore/ysql-language-features/pg-extensions/#tablefunc-example)_ extension as part of your standard set of central utilities. This does bring a random-number generator function, _normal_rand()_. However, this generates a normally distributed set of _double precision_ values. This functionality isn't appropriate for testing _modeled_age()_; but it _is_ appropriate for many other testing purposes.
{{< /tip >}}

First, you need a helper function to convert the _bytea_ value that _gen_random_bytes()_ returns to a number value. Create and test the helper thus:

```plpgsql
drop function if exists bytea_to_num(bytea) cascade;

create function bytea_to_num(b bytea)
  returns numeric
  language plpgsql
as $body$
declare
  n numeric := 0;
begin
  for j in 0..(length(b) - 1) loop
    n := n*256+get_byte(b, j);
  end loop;
  return n;
end;
$body$;
```

Now create the function _random_timestamp()_. This invokes _make_timestamp()_ with values from six successive invocations of _gen_random_bytes()_, using _bytea_to_num()_ to convert the returned values first to _numeric_ values and then to suitably constrained _int_ values for each of the _year_, _month_, _mday_, _hour_, and _min_ actual arguments and to a suitably constrained _double precision_ value for the _sec_ actual argument. Create it thus:

```plpgsql
drop function if exists random_timestamp() cascade;

create function random_timestamp()
  returns timestamp
  language plpgsql
as $body$
declare
  year   constant int       not null := greatest(1::int, mod(bytea_to_num(gen_random_bytes(2))::int, 4700));
  month  constant int       not null := greatest(1::int, mod(bytea_to_num(gen_random_bytes(2))::int,   12));
  mday   constant int       not null := greatest(1::int, mod(bytea_to_num(gen_random_bytes(2))::int,   28));
  hour   constant int       not null :=                  mod(bytea_to_num(gen_random_bytes(2))::int,   23);
  min    constant int       not null :=                  mod(bytea_to_num(gen_random_bytes(2))::int,   59);
  sec    constant numeric   not null :=                  mod(bytea_to_num(gen_random_bytes(3)),        58.987654::numeric);

  ts     constant timestamp not null := make_timestamp(year, month, mday, hour, min, sec::double precision);
begin
  return case
           when (mod(bytea_to_num(gen_random_bytes(3))::int, 2) = 1) then ts
           else                                                           (ts::text||' BC')::timestamp
         end;
end;
$body$;
```

Test it like this:

```plpgsql
select
  random_timestamp() as "ts-1",
  random_timestamp() as "ts-2";
```

Repeat this _select_ time and again. Each time, you'll see a pair of different values. Sometimes, they both have AD dates; sometimes they both have BC dates; sometimes one has an AD date and one has a BC date; sometimes _"ts-1"_ is earlier than _"ts-2"_; and sometimes _"ts-2"_ is earlier than _"ts-1"_. The value _4700_ was chosen to constrain the year because _4713 BC_ is the earliest legal _timestamp_ value. (See the table in the [Synopsis](../../../../type_datetime/#synopsis) subsection on the [Date and time data types](../../../../type_datetime/) major sections' main page.) It's sufficient for the present testing purpose that the generated _timestamp_ values are between _4700 BC_ and _4700 AD_.

### Function random_test_report_for_modeled_age()

This function invokes _random_timestamp()_ to generate two new distinct values and then uses these to invoke _modeled_age()_ and the built-in _age()_. It compares the values that they return using the `==` [strict equals](../../../date-time-data-types-semantics/type-interval/interval-utilities/#the-user-defined-strict-equals-interval-interval-operator) operator. Only if they differ, does it invoke _modeled_age_vs_age()_ to show the differing values. And if this happens, it notes that at least one difference has been seen. The expectation is that there will be no differences to report so that the final report will show simply "No failures" and therefore be maximally easily understood. As a bonus, the report shows the minimum and the maximum generated values returned by _random_timestamp()_.

{{< note title="Notice how the special manifest constants '-infinity' and 'infinity' are used." >}}
Notice how the [special manifest constants](../../../../type_datetime/#special-date-time-manifest-constants) _'-infinity'_ and _'infinity'_ are used to set the starting values for, respectively, _max_ts_ and _min_ts_. Without this text-book pattern, the loop would need to be coded more elaborately by treating the first iteration as a special case that establishes _max_ts_ and _min_ts_ as the values returned by the invocation of _random_timestamp()_ this time; only then could the second and subsequent iterations be coded using _"max_ts := greatest(max_ts, greatest(ts1, ts2));"_ and _"min_ts := least(min_ts,    least(ts1, ts2));"_.
{{< /note >}}

Create the function thus:

```plpgsql
drop function if exists random_test_report_for_modeled_age(int) cascade;

create function random_test_report_for_modeled_age(no_of_attempts in int)
  returns table(z text)
  language plpgsql
as $body$
declare
  no_failures boolean     not null := true;
  max_ts      timestamp   not null :=  '-infinity';
  min_ts      timestamp   not null :=   'infinity';
begin
  for j in 1..no_of_attempts loop
    declare
      ts1  constant timestamp not null := random_timestamp();
      ts2  constant timestamp not null := random_timestamp();

      m    constant interval  not null := modeled_age(ts1, ts2);
      a    constant interval  not null :=         age(ts1, ts2);
    begin
      max_ts := greatest(max_ts, greatest(ts1, ts2));
      min_ts :=    least(min_ts,    least(ts1, ts2));

      if m == a then
        null;
      else
        no_failures := false;
        z := modeled_age_vs_age(ts1, ts2);                          return next;
      end if;
    end;
  end loop;
  ----------------------------------------------------------------------------------------

  z := '';                                                          return next;
  z := rpad('-', 120, '-');                                         return next;
  if no_failures then
    z := 'No failures.';                                            return next;
  end if;
  z := 'max_ts: '||max_ts::text||' | min_ts: '||min_ts::text;       return next;
end;
$body$;
```

Test it first with a modest number of attempts:

```plpgsql
select z from random_test_report_for_modeled_age(1000);
```

Then increase the number of attempts to, say, one million. This takes about a minute. (No thought was given to make the test run faster. Its speed is uninteresting.) You'll see that "No failures"_ is reported and that the range of randomly generated _timestamp_ values spans close to the maximum that _random_timestamp()_ can produce (_4700 BC_ through _4700 AD_).

This should give you a very high confidence indeed that the function _modeled_age()_ lives up to its name.

## The semantics of the one-parameter moment overload of function age()

The effect of _age(t)_ is identical to the effect of _age(\<midnight today\>, t)_. Here's a demonstration of the semantics. The expression _date_trunc('day', clock_timestamp())_ is copied from the definition of _today()_ in the subsection [Consider user-defined functions rather than 'today', 'tomorrow', and 'yesterday'](../../current-date-time-moment/#consider-user-defined-functions-rather-than-today-tomorrow-and-yesterday).

Do this to test this assertion for the _timestamptz_ overloads:

```plpgsql
drop procedure if exists assert_one_parameter_overload_of_age_semantics(timestamptz) cascade;
create procedure assert_one_parameter_overload_of_age_semantics(t in timestamptz)
  language plpgsql
as $body$
declare
  age_1 constant interval not null := age(t);
  age_2 constant interval not null := age(date_trunc('day', clock_timestamp()), t);
begin
 assert age_1 = age_2, 'Assert failed';
end;
$body$;

set timezone = 'UTC';
call assert_one_parameter_overload_of_age_semantics('2007-06-24');
call assert_one_parameter_overload_of_age_semantics('2051-07-19 Europe/Helsinki');
call assert_one_parameter_overload_of_age_semantics('2007-02-01 13:42:19.12345');
call assert_one_parameter_overload_of_age_semantics(clock_timestamp());

set timezone = 'America/Los_Angeles';
call assert_one_parameter_overload_of_age_semantics('2007-06-24');
call assert_one_parameter_overload_of_age_semantics('2051-07-19 Europe/Helsinki');
call assert_one_parameter_overload_of_age_semantics('2007-02-01 13:42:19.12345');
call assert_one_parameter_overload_of_age_semantics(clock_timestamp());
```

Each _call_ statement finishes without error, showing that the assertion holds for every test

Do this to test this assertion for the plain _timestamp_ overloads:

```plpgsql
drop procedure if exists assert_one_parameter_overload_of_age_semantics(timestamp) cascade;
create procedure assert_one_parameter_overload_of_age_semantics(t in timestamp)
  language plpgsql
as $body$
declare
  age_1 constant interval not null := age(t);
  age_2 constant interval not null := age(date_trunc('day', localtimestamp), t);
begin
 assert age_1 = age_2, 'Assert failed';
end;
$body$;

set timezone = 'UTC';
call assert_one_parameter_overload_of_age_semantics('2007-02-01 13:42:19.12345');
call assert_one_parameter_overload_of_age_semantics('2007-06-24');
call assert_one_parameter_overload_of_age_semantics('2051-07-19');
call assert_one_parameter_overload_of_age_semantics(clock_timestamp());

set timezone = 'America/Los_Angeles';
call assert_one_parameter_overload_of_age_semantics('2007-02-01 13:42:19.12345');
call assert_one_parameter_overload_of_age_semantics('2007-06-24');
call assert_one_parameter_overload_of_age_semantics('2051-07-19');
call assert_one_parameter_overload_of_age_semantics(clock_timestamp());
```

Each _call_ statement finishes without error, showing that the assertion holds for every test.
