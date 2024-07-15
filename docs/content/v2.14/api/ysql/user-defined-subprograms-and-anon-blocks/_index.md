---
title: User-defined subprograms and anonymous blocks [YSQL]
headerTitle: User-defined subprograms and anonymous blocks—"language SQL" and "language plpgsql"
linkTitle: User-defined subprograms and anonymous blocks
description: Describes how YSQL supports user-defined subprograms and anonymous blocks implemented in SQL and PL/pgSQL.
image: /images/section_icons/api/ysql.png
menu:
  v2.14:
    identifier: user-defined-subprograms-and-anon-blocks
    parent: api-ysql
    weight: 300
type: indexpage
showRightNav: true
---

This topic area is often referred to as "stored procedures". This is definitely sloppy because, for example:

- Anonymous blocks are not stored.
- The distinct SQL keywords _procedure_ and _function_ express different meanings.
- You often hear "stored procedures" used interchangeably with "stored procedures and triggers". But triggers are their own distinct phenomenon and this major section doesn't mention them.

Moreover, SQL and PL/pgSQL are not the only implementation languages.

This is why the precise, but more longwinded, wording is used for this major section's title and in the explanations that it presents. Nevertheless, when users say "stored procedures" in an informal context, there is very rarely any confusion.

## User-defined subprograms

YSQL supports user-defined functions and user-defined procedures. Each of the words `function` and `procedure` is a YSQL keyword. The term of art _subprogram_ will be used as an umbrella term that denotes either a function or a procedure. It is not a YSQL keyword. A user-defined subprogram has an owner, a name, and lives in a schema. Its source code definition, and its various attributes, are persisted in the catalog.

Not every programming language distinguishes between functions and procedures with different keywords. But the distinction between the two kinds of subprogram is the same in PostgreSQL and YSQL as it is in other languages.

### Functions

A function is invoked by writing it as a term within a surrounding expression of arbitrary complexity—and this is the _only_ way to invoke a function. The degenerate case is that the function invocation is the entirety of the expression. You evaluate an expression, in SQL, by writing it as the argument of a bare `select` or at one of the many syntax spots in a more complex SQL statement where it's legal to write a placeholder in a [`prepare`](../the-sql-language/statements/perf_prepare/) statement. An expression is evaluated in PL/pgSQL source code just as it would be in other languages—as the argument of an explicit or implicit assignment. (Invoking a subprogram using an expression to provide the value for one of its arguments provides an example of implicit assignment).

A function is a syntactic peer of a variable in PL/plSQL or a column in SQL. The overwhelmingly common convention is to name variables and columns with a noun or noun phrase. (It would be very odd to see a variable called _get_time_.)  Stylists argue, therefore, that functions should also be named with a noun or noun phrase to denote the value that invocation produces.

(Notwithstanding this, there are lots of SQL built-in functions with imperative names like _generate_series()_ or _gen_random_uuid()_ rather than, say, _generated_series()_ or _generated_random_uuid()_.)

### Procedures

The purpose of a function is to _do_ something. The syntax of [`create [or replace] procedure`](../the-sql-language/statements/ddl_create_procedure/) statement therefore does not allow specifying `returns`. A procedure can be invoked only as the argument of a [`call`](../the-sql-language/statements/cmd_call/) statement—both in top-level SQL and in PL/pgSQL source code.

Stylists argue, therefore, that procedures should be named with an imperative verb or an imperative verb phrase to denote the action that the invocation performs.

A procedure _can_ have an argument whose mode is `inout`. Use this if you want to pass back, say, a success/failure status to the caller. See the subsection [Example with 'inout' arguments](../the-sql-language/statements/cmd_call/) in the `call` statement account.

## Procedures were first supported in PostgreSQL Version 11

PostgreSQL Version 10, and earlier versions, did not support procedures. Therefore, the critical distinction explained above was not supported:

- A function is invoked as a term in an expression and names a computed value (and ideally has no side-effects).
- A procedure _does_ something (i.e. its _raison d'être_ is to have side effects) and is invoked using the dedicated `call` statement.

Therefore, in Version 10 and earlier, functions allowed formal arguments with the `out` and `inout` mode; and the `returns` clause was optional. PostgreSQL is duty-bound to allow application code that ran in an older version to work in the same way in a newer version. This means that even in the _current_ version of PostgreSQL, a function can _still_ be used where procedure is the proper choice.

{{< tip title="Respect the intended distinction between functions and procedures." >}}
Yugabyte recommends that you ignore the possibility to use a function for the purpose that a procedure is intended by regarding a function's `returns` clause as mandatory and avoiding the use of `out` and `inout` arguments.
{{< /tip >}}

### Invocation syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#diagram" class="nav-link active" id="diagram-tab" data-bs-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
  <li>
    <a href="#grammar" class="nav-link" id="grammar-tab" data-bs-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="diagram" class="tab-pane fade show active" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../syntax_resources/call_procedure,fn_invocation,subprogram_arg.diagram.md" %}}
  </div>
  <div id="grammar" class="tab-pane fade" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../syntax_resources/call_procedure,fn_invocation,subprogram_arg.grammar.md" %}}
  </div>
</div>

## Anonymous blocks

You can also execute a so-called anonymous block. This is a procedure that's defined _only_ by its source code—in other words, has no name and isn't persisted in the catalog. You simply execute it immediately using the [`do`](../the-sql-language/statements/cmd_do/) SQL statement. An anonymous block differs from statements like `insert`, `update`, and `delete` in that it cannot be the object of a [`prepare`](../the-sql-language/statements/perf_prepare) statement. (However, any DML SQL statements that an anonymous block issues are implicitly prepared. And you take advantage of the preparation by repeatedly executing the same `do` statement.)

YSQL inherits, from PostgreSQL, the restriction that the implementation language for an anonymous block must be PL/pgSQL; and there are no plans for PostgreSQL to be enhanced to support other languages for anonymous blocks. The defining text of an anonymous block is governed by the grammar for the _[plpgsql_block_stmt](../syntax_resources/grammar_diagrams/#plpgsql-block-stmt)_—a particular kind of PL/pgSQL compound statement. Notice that _[plpgsql_block_stmt](../syntax_resources/grammar_diagrams/#plpgsql-block-stmt)_ denotes a rule in the [YSQL Grammar](../syntax_resources/grammar_diagrams/). When the context has established the intended meaning, the prose equivalent "block statement" will be used instead.

{{< note title="This major section, so far, describes only user-defined subprograms and anonymous blocks  that are implemented in SQL or PL/plSQL." >}}
A subsection that describes user-defined subprograms that are implemented using C will be added in a later version of this major section.
{{< /note >}}

## Creating, altering, and dropping subprograms

These are the relevant SQL statements:

- [`create [or replace] function`](../the-sql-language/statements/ddl_create_function/)
- [`alter function`](../the-sql-language/statements/ddl_alter_function/)
- [`create [or replace] procedure`](../the-sql-language/statements/ddl_create_procedure/)
- [`alter procedure`](../the-sql-language/statements/ddl_alter_procedure/)
- [`drop function`](../the-sql-language/statements/ddl_drop_function/)
- [`drop procedure`](../the-sql-language/statements/ddl_drop_procedure/)
