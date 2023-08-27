---
title: The 'pg_proc' catalog table for subprograms [YSQL]
headerTitle: The 'pg_proc' catalog table for subprograms
linkTitle: The pg_proc catalog table
description: Explains how to use the 'pg_proc' catalog table to see subprogram metadata [YSQL].
menu:
  stable:
    identifier: pg-proc-catalog-table
    parent: user-defined-subprograms-and-anon-blocks
    weight: 99
type: docs
---

## Querying 'pg_proc' explicitly

The _[pg_proc](https://www.postgresql.org/docs/11/catalog-pg-proc.html)_ section in the PostgreSQL documentation, within the [System Catalogs](https://www.postgresql.org/docs/11/catalogs.html) enclosing chapter, describes the dedicated catalog table for subprogram metadata. It's a wide table with a column for every single fact that characterizes functions and procedures.

{{< tip title="Any role can see the metadata for every object in the database." >}}
After you've connected to some database by authorizing as some role, you can query the metadata for all objects in that database irrespective of ownership and privileges—and for global phenomena, cluster-wide, like roles too. YugabyteDB inherits this behavior from PostgreSQL. The community of PostgreSQL experts, and in particular committers to the code base, consider this to be a good thing that brings no security risks.
{{< /tip >}}

Create a test function and procedure thus:

```plpgsql
drop schema if exists s1 cascade;
drop schema if exists s2 cascade;
create schema s1;
create schema s2;

create procedure s1.p(i in int)
  security definer
  set timezone = 'America/New_York'
  language plpgsql
as $body$
begin
  execute format('set my_namespace.x = %L', i::text);
end;
$body$;

create function s2.f(i in int)
  returns text
  security invoker
  immutable
  set statement_timeout = 1
  language plpgsql
as $body$
begin
  return 'Result: '||(i*2)::text;
end;
$body$;
```

Now try this example query. It assumes that you connected as the role _u1_ so that it can restrict the query by that to reduce noise. Change it to suit your test and to select the attributes that interest you.

```plpgsql
with subprograms(
  schema,
  name,
  type,
  security,
  volatility,
  settings)
as (
  select
    pronamespace::regnamespace::text,
    proname::text,
    case prokind
      when 'p' then 'procedure'
      when 'f' then 'function'
      end,
    case
      when prosecdef then 'definer'
      else 'invoker'
    end,
    case
      when provolatile = 'v' then 'volatile'
      when provolatile = 's' then 'stable'
      when provolatile = 'i' then 'immutable'
    end,
    proconfig
  from pg_proc
  where proowner::regrole::text = 'u1')
select *
from subprograms
order by
  schema,
  name;
```

The result will include at least these rows (depending on your history):

```output
 schema | name |   type    | security | volatility |          settings
--------+------+-----------+----------+------------+-----------------------------
 s1     | p    | procedure | definer  | volatile   | {TimeZone=America/New_York}
 s2     | f    | function  | invoker  | immutable  | {statement_timeout=1}
```

{{< note title="You cannot set 'volatility' for a procedure." >}}
If you use one of the keywords _volatile_, _stable_, or _immutable_ when you create or alter a procedure, then you get a clear error:

```output
42P13: invalid attribute in procedure definition
```

You might think that it's strange that the _pg_proc_ table shows _volatile_ for a procedure rather than _null_. This is just a matter of convention. The purpose of a procedure is to do something—and so it hardly makes sense to call this a _side_-effect. Nevertheless, it's reasonable to say that a procedure is inevitably _volatile_.
{{< /note >}}

## The '\df' and '\sf' meta-commands

YSQL's _ysqlsh_ command line interpreter is derived directly from the source code of PostgreSQL's _psql_ and it therefore supports the same set of meta-commands.

Unlike with, say, the \\_echo_ meta-command, you can direct the output from \\_df_ and \\_sf_ to a file with the \\_o_ meta-command.

### '\df'

The \\_df_ meta-command, when you use it bare, lists pre-determined metadata for every subprogram in the database. This is rarely useful because the number of results is typically large and often uninteresting. You can restrict the results. But you don't use SQL syntax. The rules are explained in the [ysqlsh](../../../../admin/ysqlsh/) section in the dedicated subsection for [\\_df_](../../../../admin/ysqlsh-meta-commands/#df-antws-pattern-patterns). This example lists the subprograms in the schema _s2_:

```plpgsql
\df s2.*
```

This is result (following what you created using the code above):

```output
 Schema | Name | Result data type | Argument data types | Type
--------+------+------------------+---------------------+------
 s2     | f    | text             | i integer           | func
```

### '\sf'

This produces a canonically formatted _create or replace_ statement for the nominated subprogram. The optional `+` qualifier shows the line numbers. This can be useful for debugging runtime errors.

The [\\_sf_](../../../../admin/ysqlsh-meta-commands/#sf-function-description) meta-command takes a single argument: the (optionally) fully qualified _[subprogram_name](../../../../api/ysql/syntax_resources/grammar_diagrams/#subprogram-name)_ and its signature. (Its _[subprogram_call_signature](../../../../api/ysql/syntax_resources/grammar_diagrams/#subprogram-call-signature)_ is sufficient—and it's conventional always to use just this.)

Try this example. Notice that the function _bad(int)_ is contrived to cause a runtime error. Notice, too, that the various attributes besides the source text have been written in an order (following the source text)  that, while it is legal, is unusual.

```plpgsql
drop function if exists s1.bad(numeric);
create function s1.bad(i in numeric)
  returns numeric
as $body$
declare
  one constant numeric not null := 1.0;
  r numeric not null := 0;
begin
  -- Designed to fail at runtime when invoked with zero.
  r := one/i;
  return r;
end;
$body$
set timezone = 'America/Los_Angeles'
security definer
language plpgsql;

\sf+ s1.bad(numeric)
```

This is the \\_sf_ output:

```output
        CREATE OR REPLACE FUNCTION s1.bad(i numeric)
         RETURNS numeric
         LANGUAGE plpgsql
         SECURITY DEFINER
         SET "TimeZone" TO 'America/Los_Angeles'
1       AS $function$
2       declare
3         one constant numeric not null := 1.0;
4         r numeric not null := 0;
5       begin
6         -- Designed to fail at runtime when invoked with zero.
7         r := one/i;
8         return r;
9       end;
10      $function$
```

The preamble (up to and including the line that has been numbered _1_) has been canonicalized thus:

- Keywords are rendered in upper case.
- The source text follows the preamble as the final item in the generated text.
- The ordering of the non-source-text attributes is system-generated and different from the order in the _create_ statement that defined the subprogram.
- The syntax of the _set timezone_ attribute has been canonicalized.
- The _$function$_ dollar-quote has been used to surround the source text. (Correspondingly, _$procedure$_ is used around a procedure's source text.)

You can confirm with an _ad hoc_ test that if you had already used _$function$_ within the statement that created the function that you process with \\_sf_, then the generated double quote would be changed to, say, _$function1$_. (The corresponding accommodation is made for procedures.)

You might take this as a gentle hint always to place the source text as the very last item—but not to care about the mutual ordering of the other attributes.

Now invoke _bad()_ to cause the planned runtime error:

```plpgsql
select s1.bad(0);
```

This is how the error is reported:

```output
ERROR:  division by zero
CONTEXT:  PL/pgSQL function s1.bad(numeric) line 7 at assignment
```

When the source text is long, it can often be hard to identify the erroring line (in a _.sql_ script with other statements than the one that creates the erroring subprogram) where the runtime error occurred.
