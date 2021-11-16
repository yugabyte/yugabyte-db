---
title: The moment-moment overloads of the "-" operator [YSQL]
headerTitle: The moment-moment overloads of the "-" operator for timestamptz, timestamp, and time
linkTitle: moment-moment overloads of "-"
description: Explains the semantics of the moment-moment overloads of the "-" operator for the timestamptz, timestamp, and time data types. [YSQL]
menu:
  v2.6:
    identifier: moment-moment-overloads-of-minus
    parent: interval-arithmetic
    weight: 40
isTocNested: true
showAsideToc: true
---

The function _moment_moment_subtraction()_ models the algorithm for this operation for a pair of _timestamp_ values. Create it thus:

```plpgsql
drop function if exists moment_moment_subtraction(timestamptz, timestamptz) cascade;

create function moment_moment_subtraction(t1 timestamptz, t2 timestamptz)
  returns interval
  language plpgsql
as $body$
declare
  s1       constant double precision not null := extract(epoch from t1);
  s2       constant double precision not null := extract(epoch from t2);
  i_model  constant interval         not null := justify_hours(make_interval(secs=>(s1 - s2)));

  i_actual constant interval         not null := t1 - t2;
begin
  assert i_model = i_actual, 'assert failed';
  return i_model;
end;
$body$;
```

The _extract(epoch&nbsp;from...)_ function, for a _timestamp_ argument, determines the number of seconds from the so-called start of the epoch, _'1970-01-01 00:00:00'_, to the specified moment. For a _timestamptz_ argument, the epoch simply starts, as you'd expect, at _'1970-01-01 00:00:00 -00'_. And for a _time_ argument, the epoch starts at midnight.

The semantics of the _justify_hours()_ function is explained in the section [Comparing two interval values for equality](../interval-interval-equality/). This semantics is dubious for the case of creating an _interval_ value by subtracting one _timestamptz_ value from another because you are very likely to produce a hybrid _interval_ value.

Notice that the _moment_moment_subtraction()_ function uses an _assert_ statement to check that the result of the modeled implementation agrees with that of the actual implementation. Test it like this:

```plpgsql
set timezone = 'America/Los_Angeles';
select moment_moment_subtraction(
  '2021-03-14 20:00:00'::timestamptz,'2021-03-13 18:00:00'::timestamptz);
```

This is the result:

```output
1 day 01:00:00
```

Notice that it's hybrid: it has both a _days_ component and a _seconds_ component. Now add this _interval_ value to the _timestamptz_ value that subtracted:

```plpgsql
select '2021-03-13 18:00:00'::timestamptz + '1 day 01:00:00'::interval;
```

This is the result:

```output
2021-03-14 19:00:00-07
```

You don't get back the _timestamptz_ value that you started with.

The critical feature of this example that least to what seems to be a wrong result is that 18:00 on 13-Mar-2021 in the 'America/Los_Angeles' timezone is before the "spring forward" moment when Daylight Savings Time starts, and that 20:00 and 19:00 on 14-Mar-2021 in that timezone are both after the "spring forward" moment.

It's possible to understand, and predict, this outcome (and other hybrid _interval_ arithmetic outcomes like it) by understanding the three different kinds of semantics for the moment-_interval_ overloads of the `+` and `-` operators for pure _mm_ _interval_ values, pure _dd_ _interval_ values, and pure _ss_ _interval_ values. But when you have a hybrid _dd_ and _mm_ _interval_ value, you need also to understand the priority rule: in which order are the different variations of addition/subtraction semantics done?

{{< tip title="Avoid arithmetic that uses hybrid interval semantics." >}}
Yugabyte staff members have discussed this carefully and believe that the attempt to understand and memorize these rules is counterproductive—and therefore foolish. Rather, you should decide which of the three kinds of semantics your application needs and arrange that you produce only pure, rather than hybrid, _interval_ values. It recommends, therefore, that  you adopt the practice that the section [Defining and using custom domain types to specialize the native _interval_ functionality](../../custom-interval-domains/) explains.
{{< /tip >}}