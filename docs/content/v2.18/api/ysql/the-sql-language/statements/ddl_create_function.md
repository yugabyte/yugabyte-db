---
title: Create function statement [YSQL]
headerTitle: Create function
linkTitle: CREATE FUNCTION
description: Use the CREATE FUNCTION statement to create a function in a database.
menu:
  v2.18:
    identifier: ddl_create_function
    parent: statements
type: docs
---

{{< tip title="See the dedicated 'User-defined subprograms and anonymous blocks' section." >}}
User-defined functions are part of a larger area of functionality. See this major section:

- [User-defined subprograms and anonymous blocks—"language SQL" and "language plpgsql"](../../../user-defined-subprograms-and-anon-blocks/)
{{< /tip >}}

## Synopsis

Use the `CREATE FUNCTION` statement to create a function in a database.

## Syntax

{{%ebnf%}}
  create_function,
  arg_decl_with_dflt,
  arg_decl,
  subprogram_signature,
  unalterable_fn_attribute,
  lang_name,
  subprogram_implementation,
  sql_stmt_list,
  alterable_fn_and_proc_attribute,
  alterable_fn_only_attribute,
  volatility,
  on_null_input,
  parallel_mode
{{%/ebnf%}}

<a name="make-function-returns-mandatory"></a>
{{< tip title="'Regard the 'RETURNS' clause as mandatory." >}}
The [general introduction](../../../user-defined-subprograms-and-anon-blocks/#user-defined-subprograms) to the topic of user-defined subprograms explains that, for historical reasons, you can create a function, when a creating a procedure is the proper choice, by omitting the `RETURNS` clause and by giving it `OUT` or `INOUT` arguments.

**Yugabyte recommends that you don't exploit this freedom.**

For this reason, the `RETURNS` clause is shown as mandatory in the _create_function_ syntax rule (though the PostgreSQL documentation shows it as optional).

**Yugabyte further recommends that you avoid declaring `out` or `inout` arguments for a function.**

When the purpose is to return more than just a single scalar value, you should create a dedicated composite type for the purpose and use this to declare the function's return value.
{{< /tip >}}

{{< tip title="'create function' and the 'subprogram_signature' rule." >}}
When you write a `CREATE FUNCTION` statement, you will already have decided what formal arguments it will have—i.e. for each, what will be its name, mode, data type, and optionally its default value. When, later, you alter or drop a function, you must identify it. You do this, in  [`ALTER FUNCTION`](../ddl_alter_function/) and [`DROP FUNCTION`](../ddl_drop_function/), typically by specifying just its _[subprogram_call_signature](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/#subprogram-call-signature)_. You are allowed to use the full _subprogram_signature_. But this is unconventional. Notice that the _subprogram_signature_ does not include the optional specification of default values; and you _cannot_ mention these when you alter or drop a function. The distinction between the _subprogram_signature_ and the _subprogram_call_signature_ is discussed carefully in the section [Subprogram overloading](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/).
{{< /tip >}}

## Semantics

- The meanings of the various function attributes are explained in the section [Subprogram attributes](../../../user-defined-subprograms-and-anon-blocks/subprogram-attributes/).
- A function, like other schema objects such as a table, inevitably has an owner. You cannot specify the owner explicitly when a function is created. Rather, it's defined implicitly as what the built-in _current_user_ function returns when it's invoked in the session that creates the function. This user must have the _usage_ privilege on the function's schema, its argument data types, and its return data type. You (optionally) specify the function's schema and (mandatorily) its name within its schema as the argument of the _[subprogram_name](../../../syntax_resources/grammar_diagrams/#subprogram-name)_ rule.

- If a function with the given name, schema, and argument types already exists then `CREATE FUNCTION` will draw an error unless the `CREATE OR REPLACE FUNCTION` variant is used.

- Functions with different _[subprogram_call_signatures](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/#subprogram-call-signature)_ can share the same _[subprogram_name](../../../syntax_resources/grammar_diagrams/#subprogram-name)_. (The same holds for procedures.) See section [Subprogram overloading](../../../user-defined-subprograms-and-anon-blocks/subprogram-overloading/).

- `CREATE OR REPLACE FUNCTION` doesn't change permissions that have been granted on an existing function. To use this statement, the current user must own the function, or be a member of the role that owns it.

- In contrast, if you drop and then recreate a function, the new function is not the same entity as the old one. So you will have to drop existing objects that depend upon the old function. (Dropping the function `CASCADE` achieves this.) Alternatively, `ALTER FUNCTION` can be used to change most of the attributes of an existing function.
- The languages supported by default are `SQL`, `PLPGSQL` and `C`.

<a name="create-function-grants-execute-to-public"></a>
{{< tip title="'Create function' grants 'execute' to 'public'." >}}
_Execute_ is granted automatically to _public_ when you create a new function. This is very unlikely to be want you want—and so this behavior presents a disguised security risk. Yugabyte recommends that your standard practice be to revoke this privilege immediately after creating a function.
{{< /tip >}}

{{< tip title="You cannot set the 'depends on extension' attribute with 'create function'." >}}
A function's _depends on extension_ attribute cannot be set using `CREATE [OR REPLACE] FUNCTION`. You must use [`ALTER FUNCTION`](../ddl_alter_function) to set it.
{{< /tip >}}

### Scalar functions and table functions

Use the appropriate variant of the `RETURN` clause to create either a scalar function or a table function. Notice that _scalar_ can denote not just an atomic value of data types like _int_, _numeric_, _text_, and so on (and domains based on those data types); it can also denote a single composite value like that of a user-defined type.

#### Scalar function example

Try this:

```plpgsql
create schema s;

create type s.x as (i int, t text);

create function s.f(i in int, t in text)
  returns x
  security invoker
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  return (i*2, t||t)::s.x;
end;
$body$;

WITH c as (select s.f(42, 'dog') as v)
select
  (v).i, (v).t
FROM c;
```

This is the result:

```output
 i  |   t
----+--------
 84 | dogdog
```

#### Table function example:

Try this

```plpgsql
create table s.t(k serial primary key, v text);
insert into s.t(v) values ('dog'), ('cat'), ('frog');

create function s.f()
  returns table(z text)
  security definer
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  z := 'Starting content of t '; return next;
  z := '----------------------'; return next;
  for z in (select v from s.t order by k) loop
    return next;
  end loop;

  begin
    insert into s.t(v) values ('mouse');
  exception
    when string_data_right_truncation then
      z := ''; return next;
      z := 'string_data_right_truncation caught'; return next;
  end;

  insert into s.t(v) values ('bird');

  z := ''; return next;
  z := 'Finishing content of t'; return next;
  z := '----------------------'; return next;
  for z in (select v from s.t order by k) loop
    return next;
  end loop;
end;
$body$;

\t on
select z from s.f();
\t off
```

This is the result:

```output
 Starting content of t
 ----------------------
 dog
 cat
 frog

 string_data_right_truncation caught

 Finishing content of t
 ----------------------
 dog
 cat
 frog
 bird
```

This kind of table function provides a convenient way to produce an arbitrarily formatted report that can easily be spooled to a file. This is because the _select_ output is easily accessible (in _ysqlsh_) on _stdout_—and it's correspondingly easily accessible in client-side programming languages that do SQL like say, Python. In contrast, the output from _raise info_ is tricky to capture (and definitely very hard to interleave in proper sequence with _select_ results) because it comes on _stderr_.

{{< tip title="Always name the formal arguments and write the function's body last." >}}
YSQL inherits from PostgreSQL the ability to specify the arguments only by listing their data types and to reference them in the body using the positional notation `$1`, `$2`, and so on. The earliest versions of PostgreSQL didn't allow named parameters. But the version that YSQL is based on does allow this. Your code will be very much easier to understand if you use named arguments like the example does.

The syntax rules allow you to write the alterable and unalterable attributes in any order, like this:

```plpgsql
create function s.f(i in int, t in text)
  returns s.x
  security invoker
as $body$
begin
  return (i*2, t||t)::s.x;
end;
$body$
set search_path = pg_catalog, pg_temp
language plpgsql;
```

Yugabyte recommends that you avoid exploiting this freedom and choose a standard order where, especially, you write the body last. For example, it helps readability to specify the language immediately before the body. (Try the \\_sf_ meta-command for a function that you created. It always shows the source text last, no matter what order your _create [or replace]_ used.)  Following this practice will allow you to review, and discuss, your code in a natural way by distinguishing, informally, between the function _header_ (i.e. everything that comes before the body) and the _implementation_ (i.e. the body).
{{< /tip >}}

## See also

- [`ALTER FUNCTION`](../ddl_alter_function)
- [`DROP FUNCTION`](../ddl_drop_function)
- [`CREATE PROCEDURE`](../ddl_create_procedure)
- [`ALTER PROCEDURE`](../ddl_alter_procedure)
- [`DROP PROCEDURE`](../ddl_drop_procedure)
