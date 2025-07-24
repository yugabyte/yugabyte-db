---
title: The "case" statement [YSQL]
headerTitle: The "case" statement
linkTitle:
  The "case" statement
description: Describes the syntax and semantics of the PL/pgSQL "case" statement. [YSQL].
menu:
  v2024.2_api:
    identifier: case-statement
    parent: compound-statements
    weight: 30
type: docs
showRightNav: true
---

## Syntax

{{%ebnf%}}
  plpgsql_case_stmt,
  plpgsql_searched_case_stmt,
  plpgsql_searched_when_leg,
  plpgsql_simple_case_stmt,
  plpgsql_simple_when_leg
{{%/ebnf%}}

## Semantics

Just like the [_if_ statement](../if-statement), the _case_ statement lets you specify one or several lists of executable statements so that a maximum of one of those lists will be selected. Each list is guarded by a _boolean_ _guard_expression_—and each is tested in turn, in the order in which they are written.

The _guard_expression_ is written:

- in the _searched_ _case_ statement, explicitly as an _boolean_ expression in each _plpgsql_searched_when_leg_
- in the _simple_ _case_ statement, implicitly by writing the expression on the left hand side of the implied equality operator immediately after the _case_ keyword that introduces the statement and by writing the expression on the right hand side of the implied equality operator immediately after the _when_ keyword in each _plpgsql_simple_when_leg_.

When a _guard_expression_ evaluates to _false_, the point of execution immediately moves to the next _guard_expression_, skipping the statement list that it guards. As soon as a _guard_expression_ evaluates to _true_, the statement list that it guards is executed; and on completion of that list, control passes to the first statement after the _end case_ of the _case_ statement—skipping the evaluation of any remaining _guard_expressions_. This economical testing of the _guard_expressions_ is common to many programming languages. It is a particular example of so-called short-circuit evaluation.

Critically, and in contrast to the _if_ statement's semantics, the _20000_ (_case not found_) error occurs if every _guard_ expression (whether explicit or implicit) evaluates to _false_.

Here is the template of the simplest _case_ statement:

```plpgsql
case
  when <guard_expression> then
    <guarded statement list>
end case;
```

However, this degenerate form is not useful because it has the same effect as this:

```plpgsql
assert <guard_expression>;
<guarded statement list>
```

In other words, an exception is guaranteed unless the _guard_expression_ evaluates to _true_.

The two-leg form of the _simple_ _case_ statement is useful when the target expression is a _boolean_ variable that (if the larger context is bug-free) will always be _not null_:

```plpgsql
create procedure s.p(b in boolean)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  case b
    when  true then
      <some actions>
    when false then
      <some alternative actions>
  end case;
end;
$body$;
```

Try this counter example:

```plpgsql
call s.p(null);
```

This causes the _20000_ error:

```output
ERROR:  case not found
HINT:  CASE statement is missing ELSE part.
CONTEXT:  PL/pgSQL function s.p(boolean) line 3 at CASE
```

honoring the defined _case_ statement semantics.

A _case_ statement can always be rewritten as an _if_ statement. But care must be taken to implement the _case not found_ semantics when the to-be-rewritten _case_ statement doesn't have a bare _else_ branch.

Programmers argue about their preferences for the choice between a _case_ statement and an _if_ statement. See [the **tip**](../if-statement/#case-stmt-versus-if-stmt) at the send of the _if_ statement page.
