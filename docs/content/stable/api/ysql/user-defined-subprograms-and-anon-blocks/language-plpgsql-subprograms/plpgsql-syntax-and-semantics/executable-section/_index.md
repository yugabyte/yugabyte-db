---
title: PL/pgSQL executable section [YSQL]
headerTitle: The PL/pgSQL executable section
linkTitle: Executable section
description: Describes the syntax and semantics of the PL/pgSQL executable section. [YSQL].
image: /images/section_icons/api/subsection.png
menu:
  stable:
    identifier: executable-section
    parent: plpgsql-syntax-and-semantics
    weight: 20
type: indexpage
showRightNav: true
---

## Syntax

Here is the decomposition of the _plpgsql_stmt_ grammar rule, down to its terminal rules.

{{%ebnf%}}
  plpgsql_executable_stmt,
  plpgsql_basic_stmt,
  plpgsql_compound_stmt
{{%/ebnf%}}

See the dedicated sections **[Basic PL/pgSQL executable statements](./basic-statements/)** and **[Compound PL/pgSQL executable statements](./compound-statements/)**.

## Semantics

You can use names, in the _executable section_, only if the name can be resolved. If you don't use a qualified identifier, then the name resolution is attempted within the names that the _declaration section_ of the most tightly enclosing block establishes—with the caveat that a matching name must be for an item of the kind that syntactic analysis of the to-be-resolved name has established. If name resolution fails in that most tightly enclosing scope, then it's re-attempted in the next most tightly enclosing scope—finishing (when the outermost block statement is the implementation of a subprogram and not that of a _do_ statement) with the subprogram's list of formal arguments. If a to-be-resolved name remains unresolved after failing in all these scopes, then resolution is attempted in schema scope. This account applies, too, for how the names of block statements are resolved—but, of course, these names must find resolution within the contained scopes of the outermost block statements (before, if resolution finds no match, then escaping to schema scope).

Consider this contrived example. (It relies on the accounts of the [declaration section](../declaration-section) and the [exception section](../exception-section)). Here, no names escape to schema scope.

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
      raise plpgsql_error using message := 'bad "x" in block <<b3>>';
    end if;
  exception
    when plpgsql_error then
      <<except>>declare
        msg text not null := '';
      begin
        get stacked diagnostics msg := message_text;

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
