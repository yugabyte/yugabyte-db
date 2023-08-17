---
title: The "if" statement [YSQL]
headerTitle: The "if" statement
linkTitle:
  The "if" statement
description: Describes the syntax and semantics of the PL/pgSQL "if" statement. [YSQL].
menu:
  preview:
    identifier: if-statement
    parent: compound-statements
    weight: 20
type: docs
showRightNav: true
---

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link active" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../../../../syntax_resources/user-defined-subprograms-and-anon-blocks/language-plpgsql-subprograms/plpgsql-syntax-and-semantics/executable-section/compound-statements/plpgsql_if_stmt,plpgsql_elsif_leg.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade show active" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../../../../syntax_resources/user-defined-subprograms-and-anon-blocks/language-plpgsql-subprograms/plpgsql-syntax-and-semantics/executable-section/compound-statements/plpgsql_if_stmt,plpgsql_elsif_leg.diagram.md" %}}
  </div>
</div>

## Semantics

The _if_ statement lets you specify one or several lists of executable statements so that a maximum of one of those lists will be selected. Each list is guarded by a _boolean_ _guard_expression_—and each is tested in turn, in the order in which they are written. When a _guard_expression_ evaluates to _false_, the point of execution immediately moves to the next _guard_expression_, skipping the statement list that it guards. As soon as a _guard_expression_ evaluates to _true_, the statement list that it guards is executed; and on completion of that list, control passes to the first statement after the _end if_ of the _if_ statement—skipping the evaluation of any remaining _guard_expressions_. This economical testing of the _guard_expressions_ is common to many programming languages. It is a particular example of so-called short-circuit evaluation.

Here is the template of the simplest _if_ statement:

```plpgsql
if <guard_expression> then
  <guarded statement list>
end if;
```

While you're typing in your code, you might like to omit the statement list altogether. Doing so causes neither a syntax error nor a runtime error. The omission is equivalent to the single _"null;"_ statement.

The simplest _if_ statement pattern, above, is very common and useful. The guarded statement list is either executed—or skipped. But consider an example when there are two or more alternative statement lists, like this:

```plpgsql
if <guard_expression 1> then
  <guarded statement list 1>
elsif <guard_expression 2> then
  <guarded statement list 2>
end if;
```

There are two possibilities here:
- _either_ the set of _guard_expressions_ jointly covers all possibilities
- _or_ it might not cover all possibilities.

However, it's generally hard for the reader to work out whether or not all possibilities are covered. Therefore it's hard for the reader to know whether the programmer intended:

- _either_ to handle every possibility explicitly, but  simply forgot one or several possibilities
- _or_ to do nothing for the possibilities without an explicit test.

The better approach, therefore, is always to include the bare _else_ leg in an _if_ statement that has two or more guard expressions. The intention is advertised clearly by writing the _null_ statement (or maybe by raising a user-defined exception) in the _else_ leg.

Try this:

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;

create function s.f(i in int)
  returns table(z text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  if i < 0 then
    z := 'i < 0'; return next;
  elsif i > 0 then
    z := 'i > 0'; return next;
  elsif i = 0 then
    z := 'i = 0'; return next;
  else
    raise exception using
      errcode := 'YB123',
      message := 'Unexpected. "i" must not be "null".';
  end if;
end;
$body$;

select s.f(1);
select s.f(-1);
select s.f(0);
```

These are the results. First:

```output
 i > 0
```

And then:

```output
 i < 0
```


And then:

```output
 i = 0
```

Now try the negative test:

```plpgsql
select s.f(null);
```

It causes the user-defined _YB123_ error:

```output
Unexpected. "i" must not be "null".
CONTEXT:  PL/pgSQL function s.f(integer) line 10 at RAISE
```

<a name="case-stmt-versus-if-stmt"></a>
{{< tip title="A 'case' statement is often a better choice than an 'if' statement." >}}
See the [_case_ statement](../case-statement) section. The example can be re-written as a _case_ statement. In the present scenario, when you want to raise an exception if, unexpectedly, _i_ is _null_, you can rely on the _case_ statement's native functionality and simply omit the _else_ leg, thus:

```plpgsql
create function s.f_alt(i in int)
  returns table(z text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  case
    when  i < 0 then
      z := 'i < 0'; return next;
    when i > 0 then
      z := 'i > 0'; return next;
    when i = 0 then
      z := 'i = 0'; return next;
  end case;
end;
$body$;

select s.f_alt(1);
select s.f_alt(-1);
select s.f_alt(0);
```
The results so far are identical to those for the _if_ statement alternative.

Now try the negative test:

```plpgsql
select s.f_alt(null);
```

It causes the _20000_ error:

```output
case not found
HINT:  CASE statement is missing ELSE part.
CONTEXT:  PL/pgSQL function s.f_alt(integer) line 3 at CASE
```

If you can be sure that _i_ will never be _null_ (or _should_ never be, as long as the surrounding code environment is bug-free) then _f_alt()_ is a better choice than the original _f()_ because it more clearly, and more tersely, conveys the intention to the reader. (You must assume that the reader understands the _case_ statement semantics.)

If, rather, you know that _i_ might be _null_ and know what to do in this case, you can simply add another _[plpgsql_searched_when_leg](../../../../../../syntax_resources/grammar_diagrams/#plpgsql-searched-when-leg)_:

```plpgsql
case
  when  i < 0 then
    z := 'i < 0'; return next;
  when i > 0 then
    z := 'i > 0'; return next;
  when i = 0 then
    z := 'i = 0'; return next;
  when i is null then
    <appropriate action for this case>
end case;
```

This is the critical difference:

- The _case_ formulation tells the reader, without requiring analysis of the _guard_expressions_, that if none of these evaluates to _true_, then the _case_not_found_ error will be raised.

- The _if_ formulation requires the reader to look at the joint effect of all the tests in order to determine if they do, or do not, cover all possible values of _i_.

Notice that, should _i_ be a user-defined domain with a composite base type and user-defined `>`, `<`, and `=` operators that allow comparison with _int_ values, the analysis task could be difficult.

Programmers argue about their preferences for the choice between an _if_ statement and a _case_ statement.
{{< /tip >}}
