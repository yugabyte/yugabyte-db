---
title: Functions for delaying execution [YSQL]
headerTitle: Functions for delaying execution
linkTitle: Delaying execution
description: The semantics of the functions for delaying execution. [YSQL]
menu:
  v2.14:
    identifier: delaying-execution
    parent: date-time-functions
    weight: 50
type: docs
---

The three functions _pg_sleep(double precision)_, _pg_sleep_for(interval)_, and _pg_sleep_until(timestamp_tz)_ are understood simply. The argument of _pg_sleep()_ is interpreted as the number of seconds for which to sleep. Because each has a formal parameter with a different data type, they could all have been implemented as overloads of a single function. Presumably, somebody chose to use different names somewhat whimsically. The fact that they are functions allows them to be invoked within the same SQL statement, just as the demonstration of the semantic difference between _statement_timestamp()_ and _clock_timestamp()_ does above. It's probably more common to use them within PL/pgSQL code. A procedure would have been more natural here than a function. But this has no practical consequence because the invocation _perform pg_sleep(n)_ comes to the rescue.

Notice that you cannot declare a PL/pgSQL variable with the data type _void_. (The attempt causes the _0A000_ error. This is mapped to the _feature_not_supported_ exception.) If you need to include a _pg_sleep()_ invocation in a _select into_ SQL statement in PL/pgSQL code, then you should typecast its return value to _text_. (This typecast produces the empty string.)

### function pg_sleep() returns void

Here is a simple example:

```plpgsql
select (pg_sleep(1.0::double precision)::text = '')::text;
```

The result is _true_.

### function pg_sleep_for() returns void

Here is a simple example:

```plpgsql
select (pg_sleep_for(make_interval(secs=>1))::text = '')::text;
```

The result is _true_.

### function pg_sleep_until() returns void

Here is a simple example:

```plpgsql
select (pg_sleep_until( (clock_timestamp() + make_interval(secs=>1))::timestamptz )::text = '')::text;
```

The result is _true_. Notice that _pg_sleep_until()_ function has no overloads other than the one with the single _timestamptz_ parameter. This is yet another reason always to use _timestamptz_ for moment values and only to convert to other data type values on the fly if the need arises.
