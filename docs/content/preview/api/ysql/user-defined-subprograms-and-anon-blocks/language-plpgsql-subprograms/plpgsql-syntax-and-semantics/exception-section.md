---
title: PL/pgSQL exception section [YSQL]
headerTitle: The PL/pgSQL exception section
linkTitle: Exception section
description: Describes the syntax and semantics of the PL/pgSQL exception section. [YSQL].
menu:
  preview:
    identifier: exception-section
    parent: plpgsql-syntax-and-semantics
    weight: 30
type: docs
showRightNav: true
---

## Syntax

{{%ebnf%}}
  plpgsql_exception_section,
  plpgsql_handler,
  plpgsql_handler_condition
{{%/ebnf%}}

## Semantics

The _plpgsql_exception_section_ (or "exception section" in prose) contains one or more so-called _handlers_. Briefly, when an error occurs in PL/pgSQL code, or in a SQL statement that it issues dynamically, this manifests as an _exception_. Then the point of execution immediately moves to a handler that matches the exception in the exception section of the most-tightly-enclosing _[plpgsql_block_stmt](../../../../syntax_resources/grammar_diagrams/#plpgsql-block-stmt)_. (When this happens, the exception is said to have been _caught_.) See [Exception handling in PL/pgSQL](#exception-handling-in-pl-pgsql), below, for the full account.

### The relationship between the terms of art "error" and "exception".

The term of art _error_ is used to characterize the outcome when the attempt to execute a SQL statement fails. Any putative SQL statement can, of course, cause a syntax error. And almost all syntactically correct SQL statements have the capacity to cause a semantic error. (Only a few degenerate special cases, like _"select 1"_ cannot cause an error.)

When you use _psql_ (or _ysqlsh_) a SQL error is reported, on _stderr_, in general using several lines of text—the first of which starts with the word _ERROR_. Various facts about the error are available and you can control the volume of the report with the _psql_ variable _VERBOSITY_. The legal values are _default_, _terse_, and _verbose_ and you specify your choice with the \\_set_ meta-command.

You can see the effect of the _VERBOSITY_ choice with a trivial example that provokes a run-time error:

```plpgsql
\set VERBOSITY default
select 1.0/0.0;
```

It causes this output:

```output
ERROR:  division by zero
```

When you attempt the same statement with the _verbose_ choice, you get this output:

```output
ERROR:  22012: division by zero
LOCATION:  int4div, int.c:820
```

Various client-side tools (for example, Python with _[psycopg](https://www.psycopg.org/)_) can be used to connect to a database in a PostgreSQL cluster or in a YugabyteDB cluster. Each has its own way of reporting when a SQL statement causes an error and, then, giving access to the error code and to other information about the context of the error. PL/pgSQL also has corresponding mechanisms. (But _language sql_ subprograms have no such mechanism.)

When a SQL error occurs during the execution of a PL/pgSQL program, it manifests as a so-called _exception_. Notice that every PL/pgSQL expression is evaluated by using an under-the-covers _select_ statement. See the section [PL/pgSQL's "create" time and execution model](../../plpgsql-execution-model/). It explains that, for example, the simple assignment _a := b + c;"_ is executed by using _"select $1 + $2"_, binding in the current values of _a_ and _b_. This means that the _22012_ error illustrated above, and countless other errors can occur, and manifest as an exception, while a _[plpgsql_block_stmt](../../../../syntax_resources/grammar_diagrams/#plpgsql-block-stmt)_ is executing.

### Exception handling in PL/pgSQL

This subsection explains how errors can occur and how they can be handled by PL/pgSQL code.

#### How run-time errors can occur during the execution of PL/pgSQL code

This discussion concerns only run-time errors. PL/pgSQL syntax errors most commonly occur when one of these SQL statements is issued at top level:

- the _[create procedure](../../../../the-sql-language/statements/ddl_create_procedure/)_ statement
- the _[create function](../../../../the-sql-language/statements/ddl_create_function/)_ statement
- the _[do](../../../../the-sql-language/statements/cmd_do/)_ statement (during its syntax analysis phase)

However, PL/pgSQL supports the _execute_ statement (see the _[plpgsql_dynamic_sql_stmt](../../../../syntax_resources/grammar_diagrams/#plpgsql-dynamic-sql-stmt)_ rule)—which can be used only in the _[plpgsql_executable_section](../../../../syntax_resources/grammar_diagrams/#plpgsql-executable-section)_ or the  _[plpgsql_exception_section](../../../../syntax_resources/grammar_diagrams/#plpgsql-exception-section)_. This allows the text of _any_ SQL statement to be defined dynamically and executed from PL/pgSQL code. In such a case, a PL/pgSQL syntax error that a dynamically executed _create procedure_, _create function_ or _do_ statement causes manifests as a run-time error occurring in the _plpgsql_executable_section_, or _plpgsql_exception_section_, from which the DDL is submitted.

An error can therefore occur in any one of these sections:

- the declaration section—in the evaluation of an expression or in the attempt to install the value that the expression computes into the declared variable
- the executable section—during the execution of any _[plpgsql_executable_stmt](../../../../syntax_resources/grammar_diagrams/#plpgsql-executable-stmt)_
- the exception section—during the execution of any _[plpgsql_executable_stmt](../../../../syntax_resources/grammar_diagrams/#plpgsql-executable-stmt)_

#### How to handle run-time errors in PL/pgSQL code

It helps, first, to define the term _top-level PL/pgSQL block statement_. It's this that defines the implementation for a user-defined PL/pgSQL subprogram and for a _do_ statement. Because the _[plpgsql_block_stmt](../../../../syntax_resources/grammar_diagrams/#plpgsql-block-stmt)_ is a kind of _[plpgsql_executable_stmt](../../../../syntax_resources/grammar_diagrams/#plpgsql-executable-stmt)_, the top-level PL/pgSQL block statement may contain arbitrarily many child PL/pgSQL block statements, to arbitrary depth, in a hierarchy.

When an error occurs, it manifests as an exception. An exception is identified (under the covers) by the _error code_ of the error that caused it. The current execution flow is aborted and the search begins for a so-called matching _handler_ (see the _[plpgsql_handler](../../../../syntax_resources/grammar_diagrams/#plpgsql-handler)_ rule). Here "matching" means that one (or more) of the handler's _conditions_ specifies the error code of the to-be-handled exception. The condition might specify the error code explicitly by using the keyword _sqlstate_. Or it might specify it indirectly by using the identifier for the _name_ of the exception. See the section [PostgreSQL Error Codes](https://www.postgresql.org/docs/11/errcodes-appendix.html) in the PostgreSQL documentation. It shows pairs of mappings between an _error code_ and its corresponding _name_. Condition _names_ are interpreted case-insensitively.

The special catch-all condition, denoted by the bare _others_ keyword, matches every possible error _except_ error _'P0004'_ (_assert_failure_) and error _'57014'_ (_query_canceled_). Notice that it is possible, but generally unwise, to catch these two errors explicitly. See the section [Catching "assert_failure" explicitly](../executable-section/basic-statements/assert/#catching-assert-failure-explicitly).

- When an error occurs in the executable section of a PL/pgSQL block statement, the search starts in that block's exception section (if present). If no match is found, then the search continues in the exception section (if present) of the containing PL/pgSQL block statement (if this exists).
- When an error occurs in the declaration section or the exception section of a PL/pgSQL block statement, the search starts in the exception section (if present) of the containing PL/pgSQL block statement (if this exists). Again, if no match is found, then the search continues in the exception section (if present) of the containing PL/pgSQL block statement (if this exists).
-  If the search fails all the way up through the (possible) block hierarchy and fails in the top-level PL/pgSQL block statement, then the exception remains unhandled and escapes to the calling environment as the SQL error to which it corresponds.
- In the case that the calling environment is a PL/pgSQL block statement in a separate PL/pgSQL subprogram, or in a _do_ statement, the in-flight exception continues as such and the search starts anew in the PL/pgSQL block statement from which the PL/pgSQL subprogram, or _do_ statement, where the original error occurred was invoked.

- Once a matching handler is found, the exception is defined to be handled (or _caught_) and the entire effect of the present block statement's executable section is rolled back. See the section [Handling an exception implies rolling back the effect of the block statement](./#handling-an-exception-implies-rolling-back-the-effect-of-the-block-statement). The scope of the rollback includes all statically nested block statements, together with the effect of all block statements that might have been called dynamically.
- If the search for a handler fails even up through the top-level PL/pgSQL block statement of the subprogram or _do_ statement that the client code invoked, then the error escapes to the client code.

The executable statements that define the handler are executed in the normal way and then normal execution continues after the end of the _[plpgsql_block_stmt](../../../../syntax_resources/grammar_diagrams/#plpgsql-block-stmt)_ in whose exception section the handler is found—just as it would as if that block's executable section had ended normally.

Here's a trivial example. Use any convenient sandbox database and a role that has no special attributes and that has _create_ and _connect_ on that database. Do this first:


```plpgsql
\set db <the sandbox database>
\set u  <the ordinary role>
```

Ensure a clean start:

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;
```

Create a trivial function:

```plpgsql
create function s.f()
  returns text
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  a int;
begin
  a := 1.0/0.0;
  return a::text;
exception
  when sqlstate '22012' then
    return 'Caught "Error 22012".';
end;
$body$;
```

Execute it:

```plpgsql
select s.f();
```

The _create function_ statement completes without error. And then _select s.f()_ completes without error, returning this result:

```output
 Caught "Error 22012".
```

Notice that this handler:

```plpgsql
when division_by_zero or sqlstate '2200F' or others then...
```

Because the _others_ condition will handle _any_ exception, the two preceding conditions, _division_by_zero_ and _sqlstate '2200F'_, are redundant—and therefore will confuse the reader about the code author's intention.

Here's a more elaborate example:

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;
create type s.rec as(outcome text, a numeric, b text);
```

Create a function that will be called by a second function:

```plpgsql
create function s.callee(mode in text)
  returns s.rec
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  r s.rec      := ('OK', -1, '-')::s.rec;
  x numeric    := 1.0;
  y varchar(4) := 'abcd';
  z varchar(4);
begin
  begin
    case mode
      when 'division_by_zero' then
        x := x/0.0;
      when 'string_data_right_truncation' then
        z := y||'e'::varchar(4);
      else
        x := x/2.0;
        z := 'Nice';
    end case;
  exception
    when division_by_zero then
      r.outcome := 'Caught "division_by_zero" in "s.callee()".';
      return r;
  end;
  r.a := x::text;
  r.b := z::text;
  return r;
end;
$body$;
```

Create a function to call the callee function:

```plpgsql
create function s.caller(mode in text)
  returns s.rec
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  r s.rec := ('OK', -1, '-')::s.rec;
begin
  r := s.callee(mode);
  return r;
exception
  when string_data_right_truncation then
    r.outcome :=  'Caught "string_data_right_truncation" in "s.caller()".';
    return r;
end;
$body$;
```

Invoke  the caller without provoking an error:

```plpgsql
with c(rec) as (select s.caller('benign'))
select (c.rec).outcome, (c.rec).a::text, (c.rec).b from c;
```

This is the result:

```output
 outcome |           a            |  b   
---------+------------------------+------
 OK      | 0.50000000000000000000 | Nice
```

Now provoke an error that will be handled in the callee:

```plpgsql
with c(rec) as (select s.caller('division_by_zero'))
select (c.rec).outcome, (c.rec).a::text, (c.rec).b from c;
```

This is the result:

```output
                  outcome                   | a  | b 
--------------------------------------------+----+---
 Caught "division_by_zero" in "s.callee()". | -1 | -
```

And now provoke an error for which there's no handler in the callee function and which escapes to the caller to be be handled:

```plpgsql
with c(rec) as (select s.caller('string_data_right_truncation'))
select (c.rec).outcome, (c.rec).a::text, (c.rec).b from c;
```

This is the result:

```output
                        outcome                         | a  | b 
--------------------------------------------------------+----+---
 Caught "string_data_right_truncation" in "s.caller()". | -1 | -
```

#### Handling an exception implies rolling back the effect of the block statement

When the point of execution enters a PL/pgSQL block statement, at any level of nesting, the runtime system detects whether or not it has an exception section—and if it does (and only in that case), then  it automatically creates an internally named savepoint at that moment.
- If an error occurs in the block's executable section, or if an unhandled error escapes from a subprogram or a _do_ statement that it invokes, so that the point of execution enters the exception section, and if a matching handler is found, then the runtime system issues a rollback to that internally named savepoint—and the handler code is executed. Then normal execution continues after the present block statement's end.
- But if no error occurs in the block's executable section, or if an error occurs but no matching handler is found, then the runtime system releases that internally named savepoint—and the search for a handler continues as described in the previous section.

-  Of course, if the search for a handler fails all the way up through the top-level PL/pgSQL block statement that defines the subprogram or _do_ statement that was the object of the top-level server call, then the entire effect of the server call is rolled back and the error escapes to the calling client program.

{{< note title="Performance implications for a block statement that has an exception section" >}}
The PostgreSQL documentation, in the subsection [Trapping Errors](https://www.postgresql.org/docs/11/plpgsql-control-structures.html#PLPGSQL-ERROR-TRAPPING) of the section [Control Structures](https://www.postgresql.org/docs/11/plpgsql-control-structures.html) within the [PL/pgSQL chapter](https://www.postgresql.org/docs/11/plpgsql.html), includes this note (slightly paraphrased):

> A block statement that has an _exception_ section is significantly more expensive to enter and exit than a block without one. Therefore, don't write an an _exception_ section unless you need it.

The putative cost is due to the creation of the savepoint and the subsequent release of, or rollback to, the savepoint. There has been some discussion about this advice on the _pgsql-general_ email list. See, for example, [HERE](https://www.postgresql.org/message-id/4A59B6AA01F1874283EA66C976ED51FC0E1A6B%40COENGEXCMB01.cable.comcast.com) and [HERE](https://www.postgresql.org/message-id/24CD0FA6-F58C-44B5-B3D5-FE704A63E5CA@yugabyte.com). This, from acknowledged expert Tom Lane, best summarises the responses:

> The doc note is just there to let people know that it isn't free; not to scare people away from using it at all.

It helps to distinguish between two kinds of error:

- **Expected (but regrettable) errors.** Consider a graphical interface that invites the user to register to use an application. Very commonly, the user has to invent a nickname. This might well collide with an existing nickname; and the usual response is a friendly message that explains the problem, in end-user terminology, followed by an invitation to choose a different nickname. The collision is detected, under the covers, by the violation of a unique constraint on the nickname column. The standard good practice is to write the SQL statement that inserts the row with the potentially colliding nickname in a tightly enclosing block-statement that has an exception section with a handler for the _when unique_violation_ error. This handler recognizes the problem as exactly the expected regrettable outcome that it is and then signals the client code accordingly so that it can usefully inform the user and suggest trying again.

  The rationale is that it's most reliable to handle the error exactly where it occurs, when all the context information is available. If, instead, the error is allowed to bubble up and to escape to the client code (or even to an exception handler at the end of the top-level PL/pgSQL block statement that implements the subprogram or *do* statement that the client code invoked), then the analysis task is harder and less reliable because there might be many other cases, during the execution of the entire server call, where a _unique_violation_ might occur. You can read Tom Lane's advice to mean that this pattern of exception handler use is perfectly fine.

- **Unexpected errors:** An example of such an error would occur if an attempt is made to change the content of a table that an administrator has carelessly (and recently) set to be _read-only_. Some diagnostic information about an erroring SQL statement is available only in the tightly-enclosing local context from which the statement is issued. For example, actual values that are bound to the statement for execution are no longer available where the block in which they are declared goes out of scope. A maximally defensive approach would be to issue every single SQL statement from its own dedicated tightly enclosing block statement and to provide a _when others_ handler that assembles all available context information into, say, a _jsonb_ value. This implies that every single subprogram must have an _out_ argument to return this value to the caller. The handler must also use the bare _raise_ statement (see the section [When to use the "raise" statement's different syntax variants](../executable-section/basic-statements/raise/#when-to-use-the-raise-statement-s-different-syntax-variants)) when the error report value has been created to stop execution simply blundering on.

  However, this approach would lead to verbose, cluttered, code to the extent that it would be very difficult to read and maintain. And, with so very many savepoints being created and then released during normal, error free, execution, there would be noticeable performance penalty.

  The best, and simplest, way to handle such unexpected errors is in a single _when others_ handler at the end of the top-level PL/pgSQL block statement that implements the subprogram or *do* statement that the client code invoked. This handler can marshal as many facts as are available. These facts include the _context_ which specifies the call stack at the time of the exception. See the account of the _get stacked diagnostics_ statement in the next section. These facts can then be recorded in an _incidents_ table with an auto-generated _ticket_number_ primary key. When this exception section finishes, control returns to the client. This means that there is no need, here, for a bare _raise_ statement to stop execution blundering on. Rather, it's enough to return the fact that such an unexpected error occurred together with the ticket number. The client can then report this outcome in end-user terminology.
  
  You can see a self-contained working code example that illustrates this paradigm in the [ysql-case-studies](https://github.com/YugabyteDB-Samples/ysql-case-studies) GitHub repo. Look at the [hard-shell](https://github.com/YugabyteDB-Samples/ysql-case-studies/tree/main/hard-shell) case-study and within that at the exception section in the source code for the procedure [api.insert_master_and_details()](https://github.com/YugabyteDB-Samples/ysql-case-studies/blob/main/hard-shell/60-install-api.sql#L36).
{{< /note >}}

#### How to get information about the error

The dedicated _[plpgsql_get_stacked_diagnostics_stmt](#plpgsql-get-stacked-diagnostics-stmt)_ (one of the kinds of _[plpgsql_basic_stmt](../../../../syntax_resources/grammar_diagrams/#plpgsql-basic-stmt)_), gives information about the error that the current handler caught.

{{%ebnf%}}
  plpgsql_get_stacked_diagnostics_stmt,
  plpgsql_stacked_diagnostics_item,
  plpgsql_stacked_diagnostics_item_name
{{%/ebnf%}}

The attempt to invoke it in a block statement's executable section causes the _0Z002_ run-time error:

```output
GET STACKED DIAGNOSTICS cannot be used outside an exception handler
```

Each diagnostics item is identified by a keyword that specifies a status value that will be assigned to the specified target variable (or subprogram argument). The available items are listed in the following table. Each status value has the data type _text_.

<a name="diagnostics-item-names"></a>
| **Diagnostics item name** | **Description**                                                           |
| ------------------------- | ------------------------------------------------------------------------- |
| returned_sqlstate         | The code of the error that caused the exception.                          |
| message_text              | The text of the exception's primary message.                              |
| pg_exception_detail       | The text of the exception's detail message.                               |
| pg_exception_hint         | The text of the exception's hint message.                                 |
| schema_name               | The name of the schema related to exception.                              |
| table_name                | The name of the table related to exception.                               |
| column_name               | The name of the column related to exception.                              |
| pg_datatype_name          | The name of the data type related to exception.                           |
| constraint_name           | The name of the constraint related to exception.                          |
| pg_exception_context      | Line(s) of text that specify the call stack at the time of the exception. |

Different errors define values for different subsets of the available items. When a value for a particular item isn't defined, the _get stacked diagnostics_ statement assigns the empty string (rather than _null_) to the specified target variable. This means that you can declare the target variables with a _not null_ constraint.

Here is an example. Ensure a clean start and create some supporting objects:

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;
create domain s.d as int
constraint d_ok check(value between 1 and 5);

create table s.t(
  k  int primary key,
  c1 varchar(4) constraint t_c1_nn not null,
  c2 s.d,
  constraint t_c2_nn check(c2 is not null));

insert into s.t(k, c1, c2) values(1, 'dog', 3);

create type s.diagnostics as(
  returned_sqlstate    text,
  message_text         text,
  pg_exception_detail  text,
  pg_exception_hint    text,
  schema_name          text,
  table_name           text,
  column_name          text,
  pg_datatype_name     text,
  constraint_name      text,
  pg_exception_context text
  );

create function s.show(t in text)
  returns text
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  return case t
    when '' then '-'
    else         t
  end;
end;
$body$;
```

Now create a function _s.f()_ that will provoke one of a few different contrived errors according to what its input actual argument specifies. One of these is a user-defined error that is caused by the [_raise_ statement](../executable-section/basic-statements/raise). Notice that the exception section defines just a single _others_ handler. This will populate a value of the composite type _s.diagnostics_ that has an attribute for every single available diagnostics item. The function returns the _s.diagnostics_ value so that a caller table function can display each value that _get stacked diagnostics_ populated.

```plpgsql
create function s.f(mode in text)
  returns s.diagnostics
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  diags s.diagnostics;
  err   text not null := '';
begin
  case mode
    when 'unique_violation' then
      insert into s.t(k, c1, c2) values(1, 'cat', 4);
    when 'not_null_violation' then
      update s.t set c1 = null where k = 1;
    when 'string_data_right_truncation' then
      update s.t set c1 = 'mouse' where k = 1;
    when 'check_violation 1' then
      update s.t set c2 = 7 where k = 1;
    when 'check_violation 2' then
      update s.t set c2 = null where k = 1;

    when 'raise_exception' then
      raise exception using
        errcode    := 'YB573',
        message    := 'It went wrong',
        detail     := 'Caused by "raise exception".',
        hint       := 'Do better next time.',
        schema     := 'n/a',
        table      := 'n/a',
        column     := 'n/a',
        datatype   := 'n/a',
        constraint := 'n/a' ;

    else -- No error
      update s.t set c1 = 'cat', c2 = 2 where k = 1;
  end case;

  diags.returned_sqlstate := 'No error';
  return diags;
exception
  when others then
    get stacked diagnostics
      diags.returned_sqlstate    := returned_sqlstate,
      diags.message_text         := message_text,
      diags.pg_exception_detail  := pg_exception_detail,
      diags.pg_exception_hint    := pg_exception_hint,
      diags.schema_name          := schema_name,
      diags.table_name           := table_name,
      diags.column_name          := column_name,
      diags.pg_datatype_name     := pg_datatype_name,
      diags.constraint_name      := constraint_name,
      diags.pg_exception_context := pg_exception_context;

    err := case diags.returned_sqlstate
             when '23505' then 'unique_violation'
             when '23502' then 'not_null_violation'
             when '22001' then 'string_data_right_truncation'
             when '23514' then 'check_violation'
             else              diags.returned_sqlstate
           end;
    diags.returned_sqlstate := err;
    return diags;
end;
$body$;
```

Now create the table function _s.f_outcome()_ to invoke _s.f()_ and to report the value of each of the attributes of the _s.diagnostics_ value that it returns:

```plpgsql
create function s.f_outcome(mode in text)
  returns table(z text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  diags s.diagnostics := s.f(mode);
begin
  z := rpad('sqlstate:',    13)||s.show(diags.returned_sqlstate);          return next;

  if diags.returned_sqlstate <> 'No error' then
    z := rpad('message:',     13)||s.show(diags.message_text);             return next;
    z := rpad('detail:',      13)||s.show(diags.pg_exception_detail);      return next;
    z := rpad('hint:',        13)||s.show(diags.pg_exception_hint);        return next;
    z := rpad('schema:',      13)||s.show(diags.schema_name);              return next;
    z := rpad('table:',       13)||s.show(diags.table_name);               return next;
    z := rpad('column:',      13)||s.show(diags.column_name);              return next;
    z := rpad('datatype:',    13)||s.show(diags.pg_datatype_name);         return next;
    z := rpad('constraint:',  13)||s.show(diags.constraint_name);          return next;

    z := '';                                                               return next;
    z := 'context:';                                                       return next;
    z := s.show(diags.pg_exception_context);                               return next;
  end if;
end;
$body$;
```

Now invoke it with, in turn, each of the actual values of the _mode_ formal argument that it was designed to respond to.

**1. First, cause no error:**

```plpgsql
select s.f_outcome('benign');
```

It executes this statement:

```plpgsql
update s.t set c1 = 'cat', c2 = 2 where k = 1;
```

This is the result:

```ouput
 sqlstate:    No error
```

**2. Next, cause an error:**

```plpgsql
select s.f_outcome('unique_violation');
```

It executes this statement:

```plpgsql
insert into s.t(k, c1, c2) values(1, 'cat', 4);
```

This attempts to insert a row with the same value for the primary key column, _k_ that the existing row has. This is the result:

```output
 sqlstate:    unique_violation
 message:     duplicate key value violates unique constraint "t_pkey"
 detail:      -
 hint:        -
 schema:      -
 table:       -
 column:      -
 datatype:    -
 constraint:  -
 
 context:
 PL/pgSQL function s.f(text) line 5 at statement block                                          +
 PL/pgSQL function s.f_outcome(text) line 4 during statement block local variable initialization
```

Notice the empty values. The error definitely occurs in the context of a specific table in a specific schema. And it's due to the violation of a specific constraint. So the implementation (inherited directly from PostgreSQL) could have filled in these values. However, because a primary key constraint is, in general, defined for a column list rather than for a single column, there can be no general way to fill in the column name and the data type.

You'll notice that, often, fields, that might have been filled in, are not. Such cases could, presumably, be the subject of enhancement requests for vanilla PostgreSQL.

**3. Next, cause another error:**

```plpgsql
select s.f_outcome('not_null_violation');
```

It executes this statement:

```plpgsql
update s.t set c1 = null where k = 1;
```

This attempts to update the existing row to set _c1_, which is declared with a _not null_ column constraint to _null_. This is the result:

```output
 sqlstate:    not_null_violation
 message:     null value in column "c1" violates not-null constraint
 detail:      Failing row contains (1, null, 2).
 hint:        -
 schema:      s
 table:       t
 column:      c1
 datatype:    -
 constraint:  -
 
 context:
 SQL statement "update s.t set c1 = null where k = 1"                                           +
 PL/pgSQL function s.f(text) line 10 at SQL statement                                           +
 PL/pgSQL function s.f_outcome(text) line 4 during statement block local variable initialization
```

Notice that here the names of the schema, the table, and the column _are_ filled in here; but the data type is not (even though it is known). Strangely, the _not null_ column constraint _t_c1_nn_ is not filled in either. This is probably because it isn't listed in the _pg_constraint_ catalog table. Try this:

```plpgsql
select conname
from pg_constraint
where connamespace = (select oid from pg_namespace where nspname = 's');
```

This is the result:

```output
 conname 
---------
 d_ok
 t_c2_nn
 t_pkey
```

Notice that _t_c2_nn_, which _is_ listed, is defined as a _table_ constraint rather than as a _column_ constraint.


**4. Next, cause another error:**

```plpgsql
select s.f_outcome('string_data_right_truncation');
```

It executes this statement:

```plpgsql
update s.t set c1 = 'mouse' where k = 1;
```

This attempts to update the existing row to set _c1_, which is declared as _varchar(4)_ with a five-character value. This is the result:

```output
sqlstate:    string_data_right_truncation
 message:     value too long for type character varying(4)
 detail:      -
 hint:        -
 schema:      -
 table:       -
 column:      -
 datatype:    -
 constraint:  -
 
 context:
 SQL statement "update s.t set c1 = 'mouse' where k = 1"                                        +
 PL/pgSQL function s.f(text) line 12 at SQL statement                                           +
 PL/pgSQL function s.f_outcome(text) line 4 during statement block local variable initialization
```

Here, too, the names of the schema, the table, the column, and its data type are left blank. (The implied constraint that the _varchar(4)_ declaration imposes is anonymous.)

**5. Next, cause another error:**

```plpgsql
select s.f_outcome('check_violation 1');
```

It executes this statement:

```plpgsql
update s.t set c2 = 7 where k = 1;
```

This attempts to update the existing row to set _c2_, which is declared using the _s.d_ domain—and this has an explicitly defined constraint _s.d_ok_ that checks that the value lies between 1 and 5. This is the result:

```output
 sqlstate:    check_violation
 message:     value for domain s.d violates check constraint "d_ok"
 detail:      -
 hint:        -
 schema:      s
 table:       -
 column:      -
 datatype:    d
 constraint:  d_ok
 
 context:
 SQL statement "update s.t set c2 = 7 where k = 1"                                              +
 PL/pgSQL function s.f(text) line 14 at SQL statement                                           +
 PL/pgSQL function s.f_outcome(text) line 4 during statement block local variable initialization
```

Here, the names of the data type (i.e. the domain name and the schema in which it lives) and the name of the domain's constraint _are_ filled out. But the names of table and the column are left blank.

**5. Finally, cause another error:**

```plpgsql
select s.f_outcome('check_violation 2');
```

It executes this statement:

```plpgsql
update s.t set c2 = null where k = 1;
```

This attempts to update the existing row to set _c2_ to _null_. But the table _s.t_ has a table-level constraint _t_c2_nn check(c2 is not null)_. This is the result:

```output
 sqlstate:    check_violation
 message:     new row for relation "t" violates check constraint "t_c2_nn"
 detail:      Failing row contains (1, cat, null).
 hint:        -
 schema:      s
 table:       t
 column:      -
 datatype:    -
 constraint:  t_c2_nn
 
 context:
 SQL statement "update s.t set c2 = null where k = 1"                                           +
 PL/pgSQL function s.f(text) line 16 at SQL statement                                           +
 PL/pgSQL function s.f_outcome(text) line 4 during statement block local variable initialization
```

Here, the names of the schema, the table, and the table-level constraint _are_ filled out. But the names of the column and the data type are left blank—presumably because, in general, a table level constraint is not limited to constrain the values for just a single column.
