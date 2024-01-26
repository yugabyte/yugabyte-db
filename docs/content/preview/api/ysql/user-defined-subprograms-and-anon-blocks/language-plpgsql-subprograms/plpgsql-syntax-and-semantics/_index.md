---
title: PL/pgSQL syntax and semantics [YSQL]
headerTitle: PL/pgSQL syntax and semantics
linkTitle: >
  "language plpgsql" syntax and semantics
description: Describes the syntax and semantics of the PL/pgSQL language (a.k.a. language plpgsql). [YSQL].
image: /images/section_icons/api/subsection.png
menu:
  preview:
    identifier: plpgsql-syntax-and-semantics
    parent: language-plpgsql-subprograms
    weight: 20
type: indexpage
showRightNav: true
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
declare
  -- (Optionally) the intended declarations.
  -- ...

begin
  -- The intended implementation.
  -- ...
exception
  -- (Optionally) the intended handlers.
  -- ...
end;
$body$;
```

Notice that _null;_ is a legal PL/pgSQL executable statement. Of course, it does nothing at all. You might prefer to write _null;_ explicitly to emphasize your intention. Now you can grant _execute_ on _s.p_ to the role(s) that you intend.

Each section is described in a dedicated subsection:

- **[declaration section](./declaration-section)**

- **[executable section](./executable-section)**

- **[exception section](./exception-section)**
