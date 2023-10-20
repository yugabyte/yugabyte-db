---
title: Integer for loop [YSQL]
headerTitle: The "integer for loop"
linkTitle: Integer for loop
description: Describes the syntax and semantics of the integer for loop. [YSQL]
menu:
  preview:
    identifier: integer-for-loop
    parent: loop-exit-continue
    weight: 20
type: docs
showRightNav: true
---

Here's a minimal example that shows the use of all the optional syntax elements:

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;

create function s.f()
  returns table(z text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  <<"Loop-1">>for j in reverse 60..5 by 13 loop
    z := j::text; return next;
  end loop "Loop-1";
end;
$body$;

select s.f();
```

This is the result:

```output
 60
 47
 34
 21
 8
```

A name that requires double-quoting was used as the label simply to emphasize that this choice is, of course, legal.

Notice that the _loop variable_, _j_, isn't explicitly declared. The code works because the defined semantics of this kind of loop include the implicit declaration of the _loop variable_ as an _integer_ (and, surprisingly, not as a _bigint_).

The semantics can be understood unambiguously be re-writing it as an _infinite loop_, thus:

```plpgsql
declare
  j int not null := 60;
begin
  <<"Loop-1">>loop
    exit when j < 5;
    z := j::text; return next;
    j := j - 13;
  end loop "Loop-1";
end;
```

This rewrite makes it clear that, if _j_ had been earlier declared in an enclosing block statement's _declaration_ section, then the implicitly declared _loop variable_, _j_, would simply shield it from the earlier-declared _i_ in accordance with the normal rules for nested block statements.

You can rewrite an example of any of the four other kinds of loop (the _while loop_, the _integer for loop_, the _array foreach loop_, and the _query for loop_) as an _infinite loop_. And sometimes mutual rewrite is possible between other pairs of kinds of _loop_ statement. For example, an _array foreach loop_ can be re-written as an _integer for loop_ over the array's index values. The present example could have been re-written as a _while loop_ (_while j >= 5_). The _infinite loop_ is the most flexible kind—and therefore you cannot always rewrite this kind of loop as another kind (unless you use an artificial device that adds no value—like _while true loop_).
