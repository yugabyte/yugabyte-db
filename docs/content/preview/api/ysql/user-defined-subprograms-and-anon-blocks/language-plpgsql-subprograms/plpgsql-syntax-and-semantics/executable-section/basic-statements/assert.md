---
title: The "assert" statement [YSQL]
headerTitle: The "assert" statement
linkTitle: >
  The "assert" statement
description: Describes the syntax and semantics of the PL/pgSQL "assert" statement. [YSQL].
menu:
  preview:
    identifier: assert
    parent: basic-statements
    weight: 10
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
  {{% includeMarkdown "../../../../../syntax_resources/user-defined-subprograms-and-anon-blocks/language-plpgsql-subprograms/plpgsql-syntax-and-semantics/executable-section/basic-statements/plpgsql_assert_stmt.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade show active" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../../../../syntax_resources/user-defined-subprograms-and-anon-blocks/language-plpgsql-subprograms/plpgsql-syntax-and-semantics/executable-section/basic-statements/plpgsql_assert_stmt.diagram.md" %}}
  </div>
</div>

## Semantics

The _plpgsql_assert_stmt_ evaluates its defining _boolean_expression_. If the result is _true_, then the point of execution moves silently to the next executable statement. But if the result is _false_, then the _'P0004'_ error (_assert_failure_) is caused. If the optional _text_expression_ is omitted, then the system supplied error message _"assertion failed"_ is used—in whatever national language the _lc_messages_ run-time parameter specifies. (See the PostgreSQL documentation section [Locale Support](https://www.postgresql.org/docs/11/locale.html).) If _text_expression_ is defined, then this text is used as the error message.

If an error occurs while evaluating the _boolean_expression_ or the _text_expression_, then this is reported as a normal error. The _text_expression_ is evaluated only when the assertion fails.

{{< tip title="The 'P0004' ('assert_failure') error and the '57014' ('query_canceled') error are not caught by the 'others' handler." >}}
This is a deliberate design. The idea is that these two errors should be unstoppable so that when either of them occurs, the present top-level server call from the client will be aborted. In particular, the _assert_ statement is intended for detecting program bugs (i.e. conditions that you know are impossible in the absence of bugs). Use the [_raise_ statement](../raise/) when you detect a regrettable, but nevertheless possible error like a new user-supplied value that is meant to be unique but turns out to collide with an existing value.

Testing of assertions can be enabled or disabled via the run-time parameter _plpgsql.check_asserts_, with legal values _on_ (the default) and _off_.

It is _possible_, but generally _unwise_, to catch these two errors explicitly. However, if you implement a scheme to log errors in, for example, a dedicated table then you might want to catch each of these errors, log them, and then re-raise the error. See the [_raise_ statement](../raise/) section.
{{< /tip >}}

### Catching "assert_failure" explicitly

First, try this simple test to demonstrate the "unstoppable by default" behavior:

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;

create function s.f(mode in text)
  returns text
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  a    int  not null := 0;
  v    text not null := '';
  err  text not null := '';
begin
  case mode
    when 'division_by_zero' then
      a := 1.0/0.0;
    when 'null_value_not_allowed' then
      v := null;
    when 'assert' then
      assert (1 = 2);
    end case;
  return a::text;
exception
  when division_by_zero then
    return 'Caught "division_by_zero".';
  when others then
    get stacked diagnostics err := returned_sqlstate;
    return 'Caught "Error '||err||'"';
end;
$body$;
```

Test it thus. First provoke an error for which there _is_ an explicit handler:

```plpgsql
select s.f('division_by_zero');
```

The explicit _when division_by_zero_ handler catches this, bringing this result:

```output
 Caught "division_by_zero".
```

Now provoke an error for which there is _no_ explicit handler:

```plpgsql
select s.f('null_value_not_allowed');
```

The catch-all _others_ handler catches this, bringing this result:

```output
Caught "Error 22004"
```

Now cause an assertion failure:


```plpgsql
select s.f('assert');
```

This is the result:

```outout
ERROR:  P0004: assertion failed
CONTEXT:  PL/pgSQL function s.f(text) line 13 at ASSERT
LOCATION:  exec_stmt_assert, pl_exec.c:3897
```

Now modify the implementation of _s.f()_'s exception section by adding an explicit handler for _assert_failure_. Here, _[raise info](../raise)_ is used to emulate inserting all of the information that _get stacked diagnostics_ provides into a table for subsequent off-line analysis by Support.

```plpgsql
create or replace function s.f(mode in text)
  returns text
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  a    int  not null := 0;
  v    text not null := '';
  err  text not null := '';
begin
  case mode
    when 'division_by_zero' then
      a := 1.0/0.0;
    when 'null_value_not_allowed' then
      v := null;
    when 'assert' then
      assert (1 = 2);
    end case;
  return a::text;
exception
  when division_by_zero then
    return 'Caught "division_by_zero".';
  -- Generally unwise practice. But the explicit "raise" makes this acceptable.
  when assert_failure then
    raise info 'Caught "assert_failure".';
    raise;

  when others then
    get stacked diagnostics err := returned_sqlstate;
    return 'Caught "Error '||err||'"';
end;
$body$;
```

Cause the same assertion failure now:


```plpgsql
\set VERBOSITY default
select s.f('assert');
```

This is the new result:

```output
INFO:  Caught "assert_failure".
ERROR:  assertion failed
CONTEXT:  PL/pgSQL function s.f(text) line 13 at ASSERT
```