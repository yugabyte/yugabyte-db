---
title: ALTER FUNCTION statement [YSQL]
headerTitle: ALTER FUNCTION
linkTitle: ALTER FUNCTION
description: Change properties of an existing function.
menu:
  v2.16:
    identifier: ddl_alter_function
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER FUNCTION` statement to change properties of an existing function.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-bs-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-bs-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/alter_function,subprogram_signature,arg_decl,special_fn_and_proc_attribute,alterable_fn_and_proc_attribute,alterable_fn_only_attribute,volatility,on_null_input,parallel_mode.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/alter_function,subprogram_signature,arg_decl,special_fn_and_proc_attribute,alterable_fn_and_proc_attribute,alterable_fn_only_attribute,volatility,on_null_input,parallel_mode.diagram.md" %}}
  </div>
</div>

You must identify the to-be-altered function by:

- Its name and the schema where it lives. This can be done by using its fully qualified name or by using just its bare name and letting name resolution find it in the first schema on the _search_path_ where it occurs. Notice that you don't need to (and cannot) mention the name of its owner.

- Its signature. The _[subprogram_call_signature](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/#subprogram-call-signature)_ is sufficient; and this is typically used. You can use the full _subprogram_signature_. But you should realize that the _arg_name_ and _arg_mode_ for each _arg_decl_ carry no identifying information. (This is why it is not typically used when a function or procedure is to be altered or dropped.) This is explained in the section [Subprogram overloading](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/).

## Semantics

This is explained in the section [Subprogram attributes](../../../user-defined-subprograms-and-anon-blocks/subprogram-attributes/).

## Example

Supposed that you create a function like this:

```plpgsql
drop schema if exists s3 cascade;
drop schema if exists s4 cascade;
create schema s3;

create function s3.f(i in int)
  returns text
  security definer
  volatile
  language plpgsql
as $body$
begin
  return 'Result: '||(i*2)::text;
end;
$body$;

select s3.f(17) as "s3.f(17)";
```

This is the result:

```output
  s3.f(17)
------------
 Result: 34
```

Now suppose you realise that _security definer_ was the wrong choice, that you want to mark it _immutable_, and that you want to set the _statement_timeout_ attribute (never mind that this is unrealistic here). Suppose, too, that: you want to call the function _g()_ instead of _f()_; and you want it to be in schema _s4_ and not in schema _s3_. You must use three `ALTER` statements to do this, thus:

```plpgsql
alter function s3.f(int)
  security invoker
  immutable
  set statement_timeout = 1;
```

Check the effect by inspecting the function's metadata. See the section [The «pg_proc» catalog table for subprograms](../../../user-defined-subprograms-and-anon-blocks/pg-proc-catalog-table/) for information on how to  query subprogram metadata.

```plpgsql
select
  proname::text                                as name,
  pronamespace::regnamespace::text             as schema,
  case
    when prosecdef then 'definer'
    else 'invoker'
  end                                          as security,

  case
    when provolatile = 'v' then 'volatile'
    when provolatile = 's' then 'stable'
    when provolatile = 'i' then 'immutable'
  end                                          as volatility,


  proconfig                                    as settings
from pg_proc
where
  proowner::regrole::text = 'u1' and
  proname::text in ('f', 'g');
```

This is the result:

```output
 name | schema | security | volatility |       settings
------+--------+----------+------------+-----------------------
 f    | s3     | invoker  | immutable  | {statement_timeout=1}
```

Now rename the function:

```plpgsql
alter function s3.f(int) rename to g;
```

Check the result by re-running the _pg_prpc_ query. This is new result:

```output
 name | schema | security | volatility |       settings
------+--------+----------+------------+-----------------------
 g    | s3     | invoker  | immutable  | {statement_timeout=1}
```

Now change the schema:


```plpgsql
create schema s4;
alter function s3.g(int) set schema s4;
```

Check the result by re-running the _pg_prpc_ query. This is new result:

```output
 name | schema | security | volatility |       settings
------+--------+----------+------------+-----------------------
 g    | s4     | invoker  | immutable  | {statement_timeout=1}
```

## See also

- [`CREATE FUNCTION`](../ddl_create_function)
- [`DROP FUNCTION`](../ddl_drop_function)
- [`CREATE PROCEDURE`](../ddl_create_procedure)
- [`ALTER PROCEDURE`](../ddl_alter_procedure)
- [`DROP PROCEDURE`](../ddl_drop_procedure)
