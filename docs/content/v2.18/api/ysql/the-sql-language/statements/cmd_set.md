---
title: SET statement [YSQL]
headerTitle: SET
linkTitle: SET
description: Use the SET statement to update a run-time control parameter.
menu:
  v2.18:
    identifier: cmd_set
    parent: statements
type: docs
---

## Synopsis

Use the `SET` statement to change the value of a run-time parameter.

## Syntax

{{%ebnf%}}
  set
{{%/ebnf%}}

## Semantics

The parameter values that you set with this statement apply just in the scope of a single session and for no longer than the session's duration. It's also possible to set the default values for such parameters at the level of the entire cluster or at the level of a particular database. For example:

```plpgsql
alter database demo set timezone = 'America/Los_Angeles';
```

See [`ALTER DATABASE`](../ddl_alter_db/).

### SESSION

Specify that the command affects only the current session.

### LOCAL

Specify that the command affects only the current transaction. After `COMMIT` or `ROLLBACK`, the run-time parameter takes effect again.

### *run_time_parameter*

Specify the name of a mutable run-time parameter.

You can inspect the current value of a run-time parameter with the _current_setting()_ built-in function, for example:

```plpgsql
select current_setting('search_path');
```

The term of art _setting_ is used as a convenient shorthand for _run-time parameter_. Many run-time parameters are system-defined. A system-defined run-time parameter is spelled using only latin letters and underscores. And, in any particular PostgreSQL version, the list of system-defined run-time parameters is fixed.

The `show ...` SQL statement is a shorthand for _select current_setting(...)_, for example:

```plpgsql
show search_path;
```

The `show` statement has a special variant `show all`, not available when you use _select current_setting(...)_, that lists the current value of every system-defined run-time parameter. (An alternative way to see this list, with some useful accompanying information for each setting, is to query the _pg_settings_ catalog view.)

### value

Specify the value of parameter.

### User-defined run-time parameters

You can also create a user-defined run-time parameter, on the fly, by spelling it suitably. The duration of  a user-defined run-time parameter never exceeds that of the session. The spelling _must_ include a period. And, as long as  you double-quote in in the `set` statement, it can contain any combination of arbitrary characters. Try this:

```plpgsql
set min.værelse17 = 'stue';
set "MIN.VÆRELSE17@" = 'kjøkken';
set "^我的.爱好" = '跳舞';
select
  current_setting('min.værelse17')  as s1,
  current_setting('MIN.VÆRELSE17@') as s2,
  current_setting('^我的.爱好')      as s3;
```

This is the result:

```output
  s1  |   s2    |  s3
------+---------+------
 stue | kjøkken | 跳舞
```

Notice that `show all` and querying _pg_settings_ never lists user-defined run-time parameters.

## Run-time parameters respect transactional semantics.

Try this test:

```plpgsql
set search_path = pg_catalog, pg_temp;
start transaction;
set search_path = some_schema, pg_catalog, pg_temp;
set my.value = 'something';
select current_setting('search_path');
select current_setting('my.value');
rollback;
select current_setting('search_path');
select '>'||current_setting('my.value')||'<';
```

The test outputs this:

```output
 some_schema, pg_catalog, pg_temp
```
followed by this:

```output
 something
```

when the transaction is ongoing and this:

```output
 pg_catalog, pg_temp
```

followed by this:

```output
 ><
```
following the `rollback`. This shows that the user-defined run-time parameter has been created for the duration of the session and that its value following the `rollback` is the empty string.

## See also

- [`SHOW`](../cmd_show)
- [`RESET`](../cmd_reset)
