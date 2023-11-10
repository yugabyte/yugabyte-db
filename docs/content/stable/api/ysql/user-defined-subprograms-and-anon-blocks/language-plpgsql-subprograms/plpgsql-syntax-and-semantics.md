---
title: PL/pgSQL syntax and semantics [YSQL]
headerTitle: PL/pgSQL syntax and semantics
linkTitle: plpgsql syntax and semantics
description: Describes the syntax and semantics of the PL/pgSQL language (a.k.a. language plpgsql). [YSQL].
menu:
  stable:
    identifier: plpgsql-syntax-and-semantics
    parent: language-plpgsql-subprograms
    weight: 20
type: docs
---

PostgreSQL, and therefore YSQL, natively support both _language sql_ and _language plpgsql_ functions and procedures. But the implementation of a _do_ statement can only be _language plpgsql_. PL/pgSQL source text is governed by the [_plpgsql_block_stmt_ rule](#plpgsql-block-stmt). See these sections:

- _[create function](../../../the-sql-language/statements/ddl_create_function/)_
- _[create procedure](../../../the-sql-language/statements/ddl_create_procedure/)_
- _[do](../../../the-sql-language/statements/cmd_do/)_

The syntax diagrams in these three sections show that the PL/pgSQL source text must be enquoted. Yugabyte recommends that, for consistency, you use dollar quoting around the source text and that you spell this as _$body$_. Notice that PL/pgSQL's dynamic SQL feature lets you write a user-defined procedure that will create a user-defined subprogram. If you take advantage of this, then you'll have to use different enquoting syntax around the source text of the to-be-created subprogram.

This section, and its subsections, specify:

- the grammar of the _plpgsql_block_stmt_ rule
- its decomposition down to terminal rules
- the associated semantics.

{{%ebnf%}}
  plpgsql_block_stmt,
  plpgsql_declaration_section,
  plpgsql_executable_section,
  plpgsql_exception_section
{{%/ebnf%}}

## The minimal PL/pgSQL source text

The executable section can include a block statement—and this implies the possibility of an arbitrarily deep nesting. It's this that underpins this characterization of PL/pgSQL at the start of this overall section on [_language plpgsql_ subprograms](../../language-plpgsql-subprograms/):

> PL/pgSQL is a conventional, block-structured, imperative programming language [whose] basic syntax conventions and repertoire of simple and compound statements seem to be inspired by Ada.

The executable section is mandatory. This, therefore, is the minimal form of a PL/pgSQL source text:

```output
$body$
begin

end;
$body$;
```

It's useful to know this because each of _create function_ and _create procedure_, when it completes without error, inevitably creates a subprogram upon which the _execute_ privilege has already been granted to _public_. See these tips in the sections that describe these two _create_ statements:

- ['Create function' grants 'execute' to 'public'](../../../the-sql-language/statements/ddl_create_function/#create-function-grants-execute-to-public)

- ['Create procedure' grants 'execute' to 'public'](../../../the-sql-language/statements/ddl_create_procedure/#create-procedure-grants-execute-to-public)

Each tip recommends that you always revoke this privilege immediately after creating a subprogram. However, even this might expose a momentary security risk. Here is the watertight secure approach:

```plpgsql
create schema s;

create procedure s.p()
  language plpgsql
as $body$
begin
  null; -- Implementation to follow.
end;
$body$;

revoke execute on procedure s.p() from public;

-- "create or replace" leaves the extant privileges on "s.p" unchanged.
create or replace procedure s.p()
  set search_path = pg_catalog, pg_temp
  security definer
  language plpgsql
as $body$
begin
  raise info 'The intended implementation';
  -- ...
end;
$body$;
```

Notice that _null;_ is a legal PL/pgSQL executable statement. Of course, it does nothing at all. You might prefer to write _null;_ explicitly to emphasize your intention. Now you can grant _execute_ on _s.p_ to the role(s) that you intend.


## The declaration section

{{< note title="Coming soon." >}}
This account will follow presently.
{{< /note >}}

<!--- _to_do_ --->

## The executable section

Here is the decomposition of the _plpgsql_stmt_ grammar rule, down to its terminal rules.

{{%ebnf%}}
  plpgsql_stmt,
  plpgsql_basic_stmt,
  plpgsql_compound_stmt
{{%/ebnf%}}

You can use names, in the _executable section_, only if the name can be resolved. If you don't use a qualified identifier, then the name resolution is attempted within the names that the _declaration section_ of the most tightly enclosing block establishes—with the caveat that a matching name must be for an item of the kind that syntactic analysis of the to-be-resolved name has established. If name resolution fails in that most tightly enclosing scope, then it's re-attempted in the next most tightly enclosing scope—finishing (when the outermost block statement is the implementation of a subprogram and not that of a _do_ statement) with the subprogram's list of formal arguments. If a to-be-resolved name remains unresolved after failing in all these scopes, then resolution is attempted in schema scope. This account applies, too, for how the names of block statements are resolved—but, of course, these names must find resolution within the contained scopes of the outermost block statements (before, if resolution finds no match, then escaping to schema scope).

Consider this contrived example. (It relies on the accounts of the [declaration section](#the-declaration-section) and the [exception section](#the-exception-section)). Here, no names escape to schema scope.

```plpgsql
create function f(x in text)
  returns table(z text)
  set search_path = pg_catalog, pg_temp
  security invoker
  language plpgsql
as $body$
<<b0>>declare
  s constant text not null := rpad(' ', 10);
  a constant text not null := 'b0.a';
begin
  z := '';                                                            return next;
  z := 'in <<b0>>';                                                   return next;
  z := s||'x:   '||x;                                                 return next;
  z := s||'a:   '||a;                                                 return next;

  <<b1>>declare
    x constant text not null := 'b1.x';
  begin
    z := '';                                                          return next;
    z := 'in <<b1>>';                                                 return next;
    z := s||'x:   '||x;                                               return next;
    z := s||'a:   '||a;                                               return next;

    <<b2>>declare
      x constant text not null := 'b2.x';
      a constant text not null := 'b2.a';
    begin
      z := '';                                                        return next;
      z := 'in <<b2>>';                                               return next;
      z := s||'x:   '||x;                                             return next;
      z := s||'f.x: '||f.x;                                           return next;
    end b2;
  end b1;

  <<b3>>declare
    a constant text not null := 'b3.a';
  begin
    z := '';                                                          return next;
    z := 'in <<b3>>';                                                 return next;
    z := s||'x:   '||x;                                               return next;
    if length(x) > 3 then
      raise plpgsql_error using message = 'bad "x" in block <<b3>>';
    end if;
  exception
    when plpgsql_error then
      <<except>>declare
        msg text not null := '';
      begin
        get stacked diagnostics msg = message_text;

        z := '-----';                                                 return next;
        z := '"plpgsql_error" handled for x = '||x;                   return next;
        z := 'Message: '||msg;                                        return next;
      end except;
  end b3;
end b0;
$body$;
```

The only goal of this code is pedagogy. Notice these semantic features:

- Blocks are nested. For example, _b2_ is declared within the executable section of _b1_; and _b1_ is declared within the executable section of _b0_;

- All the local variables are decorated with the keywords _not null_ and _constant_ and are given initial values, as they must be, as part of the declaration.

- The names of the local variables, _a_, and _x_ collide with names that are defined in outer scopes.

- The block statements are all labeled to allow the use of block-qualified identifiers.

- An unqualified reference to an item (formal argument or local variable) resolves to whatever the name means in the most-tightly-enclosing scope. An item in an inner scope can therefore hide an item with the same name in an outer scope.

- You can safely write _raise plpgsql_error_, and then write an exception section to handle it, knowing that only your code might raise this.


Test it first like this:

```plpgsql
select f('f.x');
```

This is the output:

```output
 in <<b0>>
           x:   f.x
           a:   b0.a

 in <<b1>>
           x:   b1.x
           a:   b0.a

 in <<b2>>
           x:   b2.x
           f.x: f.x

 in <<b3>>
           x:   f.x
```

Now test it like this:

```plpgsql
select f('f.bad');
```

This is the output:

```output
 in <<b0>>
           x:   f.bad
           a:   b0.a

 in <<b1>>
           x:   b1.x
           a:   b0.a

 in <<b2>>
           x:   b2.x
           f.x: f.bad

 in <<b3>>
           x:   f.bad
 -----
 "plpgsql_error" handled for x = f.bad
 Message: bad "x" in block <<b3>>
```

{{< note title="Coming soon." >}}
This remainder of this account will follow presently.
{{< /note >}}

<!--- _to_do_ --->

## The exception section

{{< note title="Coming soon." >}}
This account will follow presently.
{{< /note >}}

<!--- _to_do_ --->
