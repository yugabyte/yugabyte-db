---
title: Alterable subprogram attributes [YSQL]
headerTitle: Alterable subprogram attributes
linkTitle: Alterable common attributes
description: Describes and categorizes the subprogram attributes that cab be changed with the ALTER statement  [YSQL].
menu:
  v2.16:
    identifier: alterable-subprogram-attributes
    parent: subprogram-attributes
    weight: 20
type: docs
---

## Configuration parameter

This term denotes parameters like _timezone_ that you can set, within the scope of a single session, and for no longer than the session's duration, with the [`set`](../../../the-sql-language/statements/cmd_set/) statement. You observe the current value with the [`show`](../../../the-sql-language/statements/cmd_show/) statement or the _[current_setting()](https://www.postgresql.org/docs/11/functions-admin.html#FUNCTIONS-ADMIN-SET)_ built-in function.

You can execute a `set` statement in the source text of a subprogram's _[implementation_definition](../../../syntax_resources/grammar_diagrams/#implementation-definition)_. But you might prefer to make such a setting a property of the subprogram like this:

```plpgsql
drop function if exists f() cascade;
create function f()
  returns text
  set timezone = 'America/New_York'
  language sql
as $body$
  select current_setting('timezone');
$body$;

select f();
```

This is the result:

```output
        f
------------------
 America/New_York
```

## Security

The _security_ attribute determines the identity of the effective _role_ (as is observed with the _[current_user](https://www.postgresql.org/docs/11/functions-info.html#FUNCTIONS-INFO-SESSION-TABLE)_ built-in function) with which SQL issued by a subprogram executes. The allowed values are _definer_ and _invoker_. The default is _invoker_.

- A _security definer_ subprogram executes with the privileges of the subprogram's owner. And a _security invoker_ subprogram executes with the privileges of the _[session_user](https://www.postgresql.org/docs/11/functions-info.html#FUNCTIONS-INFO-SESSION-TABLE)_.

It's important to understand how unqualified names in SQL statements in a subprogram's implementation are resolved. This is explained in the [Writing SECURITY DEFINER Functions Safely](https://www.postgresql.org/docs/11/sql-createfunction.html#id-1.9.3.67.10.2) section of the PostgreSQL documentation.

Try the following demonstration. It connects to the database _demo_ and relies on two roles, _u1_ (which owns the schema called _u1_) and _u2_ (which doesn't need to own a schema).

First connect as _u1_ and create the artifacts that the demonstration needs:

```plpgsql
\c demo u1
set client_min_messages = warning;
grant usage on schema u1 to u2;

drop table if exists u1.t cascade;
drop function if exists u1.security_definer_result(int) cascade;
drop function if exists u1.security_invoker_result(int) cascade;

create table u1.t(k int primary key, v text not null);
insert into  u1.t(k, v) values(42, 'Selected value of "v" in "u1.t"');

create function u1.security_definer_result(k_in in int)
  returns table(z text)
  language plpgsql
  security definer
as $body$
begin
  z := 'session_user: '||session_user;          return next;
  z := 'current_user: '||current_user;          return next;
  z := (select v from u1.t where k = k_in);     return next;
end;
$body$;

grant execute on function security_definer_result(int) to u2;

create function u1.security_invoker_result(k_in in int)
  returns table(z text)
  security invoker
  language plpgsql
as $body$
begin
  z := 'session_user: '||session_user;                               return next;
  z := 'current_user: '||current_user;                               return next;
  z := (select v from u1.t where k = k_in);                          return next;
exception
  when insufficient_privilege then
    z := current_user||' has no privilege to select from u1.t';      return next;
end;
$body$;

grant execute on function security_invoker_result(int) to u2;
```

Now connect as _u2_ to see the behavior difference between a subprogram that has _security definer_ and one that has _security invoker_:

```plpgsql
\c demo u2
set client_min_messages = warning;

select u1.security_definer_result(42);
select u1.security_invoker_result(42);
```

This is the result:

```output
   security_definer_result
---------------------------------
 session_user: u2
 current_user: u1
 Selected value of "v" in "u1.t"

       security_invoker_result
-----------------------------------------
 session_user: u2
 current_user: u2
 u2 has no privilege to select from u1.t
```

{{< tip title="The optional 'external' keyword is allowed just for SQL conformance." >}}
Unlike in some SQL systems, `external` applies to all functions—and not just to external ones. So it actually has no effect. Yugabyte therefore recommends that you omit it.
{{< /tip >}}
