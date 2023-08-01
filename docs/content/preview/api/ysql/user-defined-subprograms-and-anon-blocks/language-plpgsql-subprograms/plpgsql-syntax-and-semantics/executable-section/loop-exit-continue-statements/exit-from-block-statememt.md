---
title: Using the "exit" statement to jump out of a block statement [YSQL]
headerTitle: Using the "exit" statement to jump out of a block statement
linkTitle: >
  Jumping out of a block statement with "exit"
description: Describes how to use the "exit" statement, not in a "loop" statement, to jump out of a block statement. [YSQL].
menu:
  preview:
    identifier: exit-from-block-statememt
    parent: loop-exit-continue-statements
    weight: 50
type: docs
showRightNav: true
---

The _exit_ statement is legal, and has well-defined semantics, within a simple block statement, even when it isn't inside a _loop_ statement. This means that you can use it anywhere in the PL/pgSQL text that defines the implementation of a _do_ statement or a _language plpgsql_ subprogram. If you label the top-level block statement that defines the implementation with, for example, _top level block_, then use can use this: 

```plpgsql
exit top_level_block when <boolean expresssion>:
```

as a more compact alternative to the bare _return_ statement within an _if_ statement:

```plpgsql
if <boolean expresssion> then
  return;
end if;
```

(The bare _return_ statement is legal, and has well-defined semantics, in a _do_ statement's implementation and in a _language plpgsql_ procedure. The other forms of the _return_ statement are legal only in a _language plpgsql_ function.)

Try this:

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;

create function s.f(n in int)
  returns text
  language plpgsql
as $body$
declare
  v text not null := 'a';
begin
  <<b1>>begin
    exit b1 when length(v) >= n;
    v := v||'b';
    exit b1 when length(v) >= n;
    v := v||'c';
    exit b1 when length(v) >= n;
    v := v||'-impossible';
  end b1;
  return v;
end;
$body$;
```

This finishes without error. Now test it. First like this:

```plpgsql
select s.f(1);
```

This is the result:

```output
 a
```

Next like this:

```plpgsql
select s.f(2);
```

This is the result:

```output
 ab
```

And finally like this:

```plpgsql
select s.f(3);
```

This is the result:

```output
 abc
```

Now try this counter example:

```plpgsql
create procedure s.bad()
  language plpgsql
as $body$
begin
  exit;
end;
$body$;
```

It causes the _42601_ syntax error:

```output
EXIT cannot be used outside a loop, unless it has a label
```

Finally try this counter example:

```plpgsql
create procedure s.bad()
  language plpgsql
as $body$
<<b1>>begin
  loop
    continue b1;
  end loop;
end b1;
$body$;
```

It, too, causes the _42601_ syntax error, but now with this wording:

```output
block label "b1" cannot be used in CONTINUE
```

This is the meaning:

- Neither the _continue_ statement (without a label) nor the _continue \<label\>_ statement can be used outside a loop.
- When the _continue \<label\>_ statement is used, as it must be, within a loop, the label must match that of an enclosing _loop_ statement.

There's an example of the legal use of the _continue \<label\>_ statement, where the label matches that of an enclosing _loop_ statement, in the function _s.vowels_from_lines()_ on the page  «Two case studies: Using the "for" loop, the "foreach" loop, the "infinite" loop, the "exit" statement, and the "continue" statement» in [Case study #2](../two-case-studies/#case-study-2-compose-a-string-of-a-specified-number-of-vowels-from-each-text-line-in-an-array-until-a-specified-number-of-such-vowel-strings-have-been-composed).

Look for this:

```plpgsql
continue lines when c = prev;
```
