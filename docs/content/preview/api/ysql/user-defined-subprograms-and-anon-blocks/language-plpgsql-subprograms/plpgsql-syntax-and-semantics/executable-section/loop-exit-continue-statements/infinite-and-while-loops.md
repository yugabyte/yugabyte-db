---
title: Infinite and while loops [YSQL]
headerTitle: The "infinite loop" and the "while loop"
linkTitle: Infinite and while loops
description: Describes the syntax and semantics of the "infinite loop" and the "while loop" [YSQL]
menu:
  preview:
    identifier: infinite-and-while-loops
    parent: loop-exit-continue-statements
    weight: 10
type: docs
showRightNav: true
---

This page describes the two kinds of _unbounded_ loop.

## "Infinite loop"

The _infinite loop_ is the simplest form of the _unbounded loop_. It looks like this:

```plpgsql
<<label_17>>loop
  <statement list 1>
  exit <label> when <boolean expression>;
  <statement list 2>
end loop label_17;
```

or this:

```plpgsql
<<label_17>>loop
  <statement list 1>
  continue label_17 when <boolean expression 1>;
  <statement list 2>
  exit label_17 when <boolean expression 2>;
  <statement list 3>
end loop label_17;
```

An _infinite loop_ must have either an _exit_ statement (or, though these are rare practices, a bare _return_ statement or a _raise exception_ statement). Otherwise, it will simply iterate forever.

## "While loop"

The other form of the _unbounded loop_ is the _while loop_. It looks like this:

```plpgsql
<<label_42>>while <boolean expression> loop
  <statement list>
end loop label_42;
```

The _boolean_ expression is evaluated before starting an iteration. If it evaluates to _false_ on entry, then no iterations take place. Otherwise, the code inside the loop had better change the outcome of the _boolean_ expression so that it eventually becomes _false_.

In this example, _label_42_ isn't mentioned in an _exit_ statement or a _continue_ statement. But the name used at the end of the _loop_ statement must anyway match the name used at its start. (If they don't match, then you get the _42601_ syntax error.)

{{< tip title="Label all loops." >}}
Many programmers recommend labelling all loops and argue that doing so improves the code's readability—precisely because the syntax check guarantees that you can be certain where a _loop_ statement ends, even when it extends over many lines and even if the author has make a mistake with the indentation.
{{< /tip >}}

Notice that this is legal:

```plpgsql
<<strange>>while true loop
  <statement list>
  continue <label> when <boolean expression>;
  <statement list>
  exit <label> when <boolean expression>;
  <statement list>
end loop strange;
```

However, the effect of writing _"while true loop"_ is indistinguishable from the effect of writing just _"loop"_. Using the verbose form is therefore pointless; and it's likely that doing so will simply confuse the reader.

Try this example:

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;
create table s.t(k serial primary key, v int not null);
insert into s.t(v) select generate_series(0, 99, 5);

create function s.f(k_lo in int, k_hi in int)
  returns table(k int, v int)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  cur refcursor not null := 'cur';
begin
  open cur no scroll for (
    select t.k, t.v
    from s.t
    where t.k between k_lo and k_hi
    order by t.k);

  found := true;
  while found loop
    fetch next from cur into k, v;      return next;
  end loop;
  close cur;
end;
$body$;

select k, v from s.f(6, 11);
```

This is the result:

```outout
 k  | v  
----+----
  6 | 25
  7 | 30
  8 | 35
  9 | 40
 10 | 45
 11 | 50
```

Notice the use of the special built-in variable _found_. (This is described in the section [The "get diagnostics" statement](../../../executable-section/basic-statements/get-diagnostics/).)

<!--- _to_do_ refer to PL/pgSQL rules for open, fetch into, and close. --->

See also the section [Beware Issue #6514](../../../../../../cursors/#beware-issue-6514) at the end of the [Cursors](../../../../../../cursors/) section. Because of the current restrictions that it describes, and because of the fact that _fetch all_ is anyway not supported in PL/pgSQL in vanilla PostgreSQL, the only viable cursor operation in PL/pgSQL besides _open_ and _close_ is _fetch next... into_. Given this, the _while loop_ approach for iterating over the results of a query shown here adds no value over what the _[query for loop](../query-for-loop/)_ brings.
