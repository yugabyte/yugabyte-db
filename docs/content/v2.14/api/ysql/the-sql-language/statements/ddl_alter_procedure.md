---
title: ALTER PROCEDURE statement [YSQL]
headerTitle: ALTER PROCEDURE
linkTitle: ALTER PROCEDURE
description: Change properties of an existing procedure.
menu:
  v2.14:
    identifier: ddl_alter_procedure
    parent: statements
type: docs
---

## Synopsis

Use the `ALTER PROCEDURE` statement to change properties of an existing procedure.

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/alter_procedure,subprogram_signature,arg_decl,special_fn_and_proc_attribute,alterable_fn_and_proc_attribute.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/alter_procedure,subprogram_signature,arg_decl,special_fn_and_proc_attribute,alterable_fn_and_proc_attribute.diagram.md" %}}
  </div>
</div>

You must identify the to-be-altered procedure by:

- Its name and the schema where it lives. This can be done by using its fully qualified name or by using just its bare name and letting name resolution find it in the first schema on the _search_path_ where it occurs. Notice that you don't need to (and cannot) mention the name of its owner.

- Its signature. The _[subprogram_call_signature](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/#subprogram-call-signature)_ is sufficient; and this is typically used. You can use the full _subprogram_signature_. But you should realize that the _arg_name_ and _arg_mode_ for each _arg_decl_ carry no identifying information. (This is why it is not typically used when a function or procedure is to be altered or dropped.) This is explained in the section [Subprogram overloading](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/).

## Semantics

This is explained in the section [Subprogram attributes](../../../user-defined-subprograms-and-anon-blocks/subprogram-attributes/).

## Example

Supposed that you create a procedure like this:

```plpgsql
drop schema if exists s1 cascade;
drop schema if exists s2 cascade;
create schema s1;

create procedure s1.p(i in int)
  security definer
  language plpgsql
as $body$
begin
  execute format('set my_namespace.x = %L', i::text);
end;
$body$;

call s1.p(42);
select current_setting('my_namespace.x')::int as "my_namespace.x";
```

This is the result:

```output
 my_namespace.x
----------------
             42
```

Now suppose you realise that _security definer_ was the wrong choice and that you want to set the _statement_timeout_ attribute (never mind that this is unrealistic here). Suppose, too, that: you want to call the procedure _q()_ instead of _p()_; and you want it to be in schema _s2_ and not in schema _s1_. You must use three `ALTER` statements to do this, thus:

```plpgsql
alter procedure s1.p(int)
  security invoker
  set statement_timeout = 1;
```

The attempt draws a warning in the current _preview_ version of YugabyteDB, thus:

```output
0A000: ALTER PROCEDURE not supported yet
```

and the _hint_ refers you to [GitHub Issue #2717](https://github.com/YugaByte/yugabyte-db/issues/2717)

In spite of the warning, the attempt actually has the intended effect. You can see this by inspecting the procedure's metadata. See the section [The «pg_proc» catalog table for subprograms](../../../user-defined-subprograms-and-anon-blocks/pg-proc-catalog-table/) for information on how to  query subprogram metadata.

```plpgsql
select
  proname::text                     as name,
  pronamespace::regnamespace::text  as schema,
  case
    when prosecdef then 'definer'
    else 'invoker'
  end                               as security,
  proconfig                         as settings
from pg_proc
where
  proowner::regrole::text = 'u1' and
  proname::text in ('p', 'q');
```

This is the result:

```output
 name | schema | security |       settings
------+--------+----------+-----------------------
 p    | s1     | invoker  | {statement_timeout=1}
```

Now rename the procedure:

```plpgsql
alter procedure s1.p(int) rename to q;
```

You get the _0A000_ warning (_"not supported yet"_) again. But, again, you get the intended result. Confirm this by re-running the _pg_prpc_ query:

This is new result:

```output
 name | schema | security |       settings
------+--------+----------+-----------------------
 q    | s1     | invoker  | {statement_timeout=1}
```

Now change the schema:


```plpgsql
create schema s2;
alter procedure s1.q(int) set schema s2;
```

This time you get a differently spelled warning:

```output
0A000: ALTER PROCEDURE SET SCHEMA not supported yet
```

but when you check the procedure's metadata you see, once again, that the schema is actually changed as intended:

```output
 name | schema | security |       settings
------+--------+----------+-----------------------
 q    | s2     | invoker  | {statement_timeout=1}
```

## See also

- [`CREATE PROCEDURE`](../ddl_create_procedure)
- [`DROP PROCEDURE`](../ddl_drop_procedure)
- [`CREATE FUNCTION`](../ddl_create_function)
- [`ALTER FUNCTION`](../ddl_alter_function)
- [`DROP FUNCTION`](../ddl_drop_function)
