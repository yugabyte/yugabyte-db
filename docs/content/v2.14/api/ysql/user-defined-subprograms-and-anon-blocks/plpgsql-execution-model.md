---
title: PL/pgSQL's execution model [YSQL]
headerTitle: PL/pgSQL's execution model
linkTitle: PL/pgSQL's execution model
description: Describes PL/pgSQL's execution model [YSQL].
menu:
  v2.14:
    identifier: plpgsql-execution-model
    parent: user-defined-subprograms-and-anon-blocks
    weight: 40
type: docs
---

## What does creating or replacing a subprogram do?

The SQL statements `create [or replace] function` and `create [or replace] procedure` do no more than a syntax check. If the check fails, then the statement has no effect. An error-free `create` will store the subprogram's definition in the catalog as its source text and other attributes spread among suitable dedicated columns in _[pg_proc](../pg-proc-catalog-table/)_. And a `create` that fails will leave _pg_proc_ unchanged. Similarly, an error-free `create or replace` will update the subprogram's row in _pg_proc_; and a `create or replace` will leave the subprogram's existing row unchanged.

## Execution model

This wording is taken (but re-written slightly) from the section [PL/pgSQL under the Hood](https://www.postgresql.org/docs/11/plpgsql-implementation.html) in the PostgreSQL documentation:

> The first time that a subprogram is called _within each session_. the PL/pgSQL interpreter fetches the subprogram's definition from _pg_proc_, parses the source text, and produces an _abstract syntax tree_ (a.k.a. _AST_). The AST fully translates the PL/pgSQL statement structure and control flow, but individual expressions and complete SQL statements used in the subprogram are not translated immediately. (Every PL/pgSQL expression is evaluated as a SQL expression.)
>
> As each expression and SQL statement is first executed in the subprogram, the PL/pgSQL interpreter parses and analyzes it to create a prepared statement.

Successive subsequent executions of a particular subprogram in a particular session will, in general, prepare more and more of its SQL statements and expressions as each new execution takes a different control-flow path.

This model brings huge understandability and usability benefits to the application developer:

- The identical set of data types, with identical semantics, is available in both top-level SQL and in PL/pgSQL.
- Expression syntax and semantics are identical in both top-level SQL and in PL/pgSQL.

But the model also brings the application developer some drawbacks:

- Errors in a specific expression or SQL statement cannot be detected until runtime, and then not until (or unless) it is reached—according to the current execution's control flow. Some particular error might therefore remain undetected for a long time, even after a subprogram is deployed into the production system.

- You must track functional dependencies like subprogram-upon-table, subprogram-upon-subprogram, and so on manually in your own external documentation. And you must drive your plan for making the cascade of accommodating changes to the closure of _functionally_ dependent subprograms, when an object is changed, entirely from this documentation.