---
title: User-defined subprograms and anonymous blocks [YSQL]
headerTitle: User-defined subprograms and anonymous blocks—"language SQL" and "language plpgsql"
linkTitle: User-defined subprograms and anonymous blocks
description: Describes how YSQL supports user-defined subprograms and anonymous blocks implemented in SQL and PL/pgSQL.
image: /images/section_icons/api/subsection.png
menu:
  v2.20:
    identifier: user-defined-subprograms-and-anon-blocks
    parent: api-ysql
    weight: 70
type: indexpage
showRightNav: true
---

This topic area is often referred to as "stored procedures". This is definitely sloppy because, for example:

- Anonymous blocks are not stored.
- The distinct SQL keywords _procedure_ and _function_ express different meanings.
- You often hear "stored procedures" used interchangeably with "stored procedures and triggers". But triggers are their own distinct phenomenon and this major section doesn't mention them.

Moreover, SQL and PL/pgSQL are not the only implementation languages.

This is why the precise, but more longwinded, wording is used for this major section's title and in the explanations that it presents. Nevertheless, when users say "stored procedures" in an informal context, there is very rarely any confusion.

{{< note title="This major section describes only user-defined subprograms and anonymous blocks that are implemented in SQL or PL/pgSQL." >}}
A subsection that describes user-defined subprograms that are implemented using C will be added in a later version of this major section.
{{< /note >}}

{{< tip title="Always immediately revoke 'execute' from any newly-created subprogram." >}}
For historical reasons, _execute_ is implicitly granted to _public_ on a newly-created subprogram. You must therefore follow every _create function s.f()_ and _create procedure s.p()_ statement with something like this:

```output
revoke all on function s.f() from public;
```

and then, maybe later on during the code installation flow, something like this:

```output
grant execute on function s.f() to ...
```
{{< /tip >}}

## User-defined subprograms

YSQL supports user-defined functions and user-defined procedures. Each of the words _function_ and _procedure_ is a YSQL keyword. The term of art _subprogram_ will be used as an umbrella term that denotes either a function or a procedure. It is not a YSQL keyword. A user-defined subprogram has an owner, a name, and lives in a schema. Its source code definition, and its various attributes, are persisted in the catalog.

Not every programming language distinguishes between functions and procedures with different keywords. But the distinction between the two kinds of subprogram is the same in [PostgreSQL](https://www.yugabyte.com/postgresql/) and YSQL as it is in other languages.

{{< note title="The YSQL documentation uses the term 'subprogram' where the PostgreSQL documentation uses 'routine'." >}}
The two terms of art, _subprogram_ and _routine_, are used when writing about programming to mean the same thing. The YSQL documentation uses _subprogram_; and the PostgreSQL documentation uses _routine_). Some SQL statements support the keyword _routine_ where they accept either _function_ or _procedure_—for example:

```output
grant execute on routine s.f(int) to r;
drop routine s.f(int);
```

Other statements do not allow this. For example, there is no _create routine_ statement. Yugabyte recommends that you avoid using _routine_ in SQL statements because it's unrealistic to impose a programming rule _never_ to use _procedure_ or _function_ where _routine_ is allowed. Such resulting mixed use can only make searching more tricky.{{< /note >}}

### Implementation languages

YSQL supports two native implementation languages for user-defined subprograms: _[language sql](./language-sql-subprograms)_; and _[language plpgsql](./language-plpgsql-subprograms)_. It supports only one language for [anonymous blocks](./#anonymous-blocks): _language plpgsql_.

### Functions

A function produces a (possibly compound) value and is invoked by writing it as a term within a surrounding expression of arbitrary complexity—and this is the _only_ way to invoke a function. The degenerate case is that the function invocation is the entirety of the expression. You evaluate an expression, in SQL, by writing it as the argument of a bare _select_ or at one of the many syntax spots in a more complex SQL statement where it's legal to write a placeholder in a _[prepare](../the-sql-language/statements/perf_prepare/)_ statement. An expression is evaluated in PL/pgSQL source code just as it would be in other languages—as the argument of an explicit or implicit assignment. (Invoking a subprogram using an expression to provide the value for one of its arguments provides an example of implicit assignment.)

A function is a syntactic peer of a variable in PL/pgSQL or a column in SQL. The overwhelmingly common convention is to name variables and columns with a noun or noun phrase. (It would be very odd to see a variable called _get_time_.)  Stylists argue, therefore, that functions should also be named with a noun or noun phrase to denote the value that invoking the function produces.

(Notwithstanding this, there are lots of SQL built-in functions with imperative names like _generate_series()_ or _gen_random_uuid()_ rather than, say, _generated_series()_ or _generated_random_uuid()_.)

### Procedures

The purpose of a procedure is to _do_ something. The syntax of the _[create [or replace] procedure](../the-sql-language/statements/ddl_create_procedure/)_ statement therefore does not allow specifying _returns_ in its declaration. A procedure can be invoked only as the argument of a _[call](../the-sql-language/statements/cmd_call/)_ statement—both in top-level SQL and in PL/pgSQL source code.

Stylists argue, therefore, that procedures should be named with an imperative verb or verb phrase to denote the action that the invocation performs.

A procedure _can_ have argument(s) whose mode is _inout_. Use this option if you want to pass back, say, a success/failure status to the caller. See the subsection [Example with 'inout' arguments](../the-sql-language/statements/cmd_call/#example-with-inout-arguments) in the _call_ statement account.

### Procedures were first supported in PostgreSQL Version 11

PostgreSQL Version 10, and earlier versions, did not support procedures. Therefore, the critical distinction explained above was not supported:

- A function is invoked as a term in an expression and names a computed value (and ideally has no side effects).
- A procedure _does_ something (i.e. its _raison d'être_ is to have side effects) and is invoked using the dedicated _call_ statement.

Therefore, in PostgreSQL Version 10 and earlier, functions allowed formal arguments with the _out_ and _inout_ mode; and the _returns_ clause was optional. (It's also possible to create a function that has _returns void_. This has the same effect as a single data type were specified and _null_ were returned.) PostgreSQL is duty-bound to allow application code that ran in an older version to work in the same way in a newer version. This means that even in the _current_ version of PostgreSQL, a function can _still_ be used where procedure is the proper choice.

{{< tip title="Respect the intended distinction between functions and procedures." >}}
Yugabyte recommends that you avoid the possibility to use a function for the purpose that a procedure is intended by regarding a function's _returns_ clause as mandatory and avoiding the use of _out_ and _inout_ arguments. This implies that, for a table function, you should prefer _returns table(...)_ over _returns setof_. The latter requires a list of _out_ arguments that correspond to the columns that you list, when you use the former, within the parenthesis of _table(...)_.
{{< /tip >}}

### Procedure invocation syntax

{{%ebnf%}}
  call_procedure,
  actual_arg
{{%/ebnf%}}

## Anonymous blocks

You can also execute a so-called anonymous block. This is a procedure that's defined _only_ by its source code—in other words, has no name and isn't persisted in the catalog. You simply execute it immediately using the _[do](../the-sql-language/statements/cmd_do/)_ SQL statement. An anonymous block differs from statements like _insert_, _update_, and _delete_ in that it cannot be the object of a _[prepare](../the-sql-language/statements/perf_prepare)_ statement. (However, any DML SQL statements that an anonymous block issues are implicitly prepared. And you take advantage of the preparation by repeatedly executing the same _do_ statement.)

YSQL inherits, from PostgreSQL, the restriction that the implementation language for an anonymous block must be PL/pgSQL; and there are no plans for PostgreSQL to be enhanced to support other languages for anonymous blocks. The defining text of an anonymous block is governed by the grammar for the _[plpgsql_block_stmt](../syntax_resources/grammar_diagrams/#plpgsql-block-stmt)_—a particular kind of PL/pgSQL compound statement. Notice that _[plpgsql_block_stmt](../syntax_resources/grammar_diagrams/#plpgsql-block-stmt)_ denotes a rule in the [YSQL Grammar](../syntax_resources/grammar_diagrams/). When the context has established the intended meaning, the prose equivalent "block statement" will be used instead.

Because an anonymous block cannot be the target of a _prepare_ statement, it cannot be parameterized. There are therefore very few use cases where using an anonymous block rather than using a procedure is a sensible choice unless you want to use it no more than once so that parameterization is not needed. Remember that a requirement not to create a persistent schema-object can be met by using a temporary user-defined procedure. See the section [Creating and using temporary schema-objects](../the-sql-language/creating-and-using-temporary-schema-objects/).

## Why use user-defined subprograms?

Some development shops avoid the use of user-defined subprograms altogether and use only top-level SQL statements as the client-side code's API to the database. And they manage to built fully-functional applications by sticking strictly to this paradigm. This implies, then, that the use of user-defined subprograms is _optional_—and this, in turn, implies the need to explain the benefits of using them. The explanation is well rehearsed both in the documentation and general marketing literature of suppliers of practical RDBMSs and in no end of third-party text books and blogs. It rests on these main points:

- The run-time SQL statements, _select_, _insert_, _update_, _delete_, and _commit_ implement primitive direct data manipulation operations. Often, one of these by itself is not enough to implement a particular business requirement.
- User-defined procedures encapsulate one or several primitive direct data manipulation operations to perform specified _atomic_ business transactions that typically make coordinated changes to the contents of several tables.
- User-defined functions encapsulate complex queries that typically access several tables rather like a view does but with the critical benefit that a function can be parameterized while a view cannot. Such complex queries implement specified business requirements.

In other words, a pure SQL API implements primitive operations that are specified in terms of data; and an API defined by user-defined subprograms implements higher-level operations that are specified in terms of business purpose. Just like with any layered API scheme, the higher level of abstraction (here the business purpose level) can be implemented by several different variants of the lower level of abstraction scheme (here the data level) so that the design of the lower level scheme, whatever variant is chosen, is hidden behind the higher level scheme's API. This implies that the details of the lower level scheme can be changed (for example to improve performance) while the higher level API specification remains unchanged. The data level API therefore becomes an _internal_ API.

Against this background, the benefits of using user-defined subprograms are clear:

- A subprogram API to the database hides all the details of the implementation like the following from client-side code: the names of the tables and their columns; the names of schemas that house them; the names of the owners of these objects; the existence of indexes, constraints, and triggers; and the SQL statements that persist and retrieve the data.
- Client code will have no privileges on any of the artifacts that implement the internal data level API. Rather, they will have only the _execute_ privilege on the subprograms that implement the business purpose API. This implies that the subprograms will be created with _security definer_ mode—and not with _security invoker_ mode. (See the [section that describes the _security_ subprogram attribute](./subprogram-attributes/alterable-subprogram-attributes/#security).)
- Because the engineers who implement the database's subprogram API own all of the code for persisting and changing the data, they are uniquely empowered to take full responsibility for the data's correctness. Client code is empower to perform _only_ the changes that the business specifies—and unspecified changes are therefore simply impossible.
- The subprogram encapsulation means, too, that every business transaction is done with just a single client-server round trip so that intermediate results that the client does not need do not have to be marshaled or transferred between server and client. This brings a performance benefit relative to an approach that invokes each of the low-level SQL statements that are needed to implement a business transaction in its own client-server call.

Everybody who works with software will recognize that this is nothing other than the decades-old paradigm of exposing a module, in the overall context of modular software design, via an API that's defined by a set of purpose-oriented subprograms—and hiding all the implementation details behind that API. Here, the module is the database that implements the overall application's data persistence and retrieval requirements. (Other modules will implement other requirements like, for example, managing the end-user's graphic interface to the application's functionality.)

## "Hard shell" case-study

The approach to overall application design that hides everything about SQL statements and what these operate on from client-side code behind an API that's implemented as user-defined subprograms is sometimes referred to as the _"hard shell"_ approach. (The metaphor emphasizes the impenetrability of the procedural encapsulation.) A self-contained implementation of such a scheme is available for you to download, study, and run here:

- **[ysql-case-studies/hard-shell](https://github.com/YugabyteDB-Samples/ysql-case-studies/tree/main/hard-shell)**

This case-study is one among several. You can install all of them in a dedicated cluster that uses several databases. The overall framework implements a convention-based scheme that guarantees that the roles that own the objects that jointly implement a particular case-study can own objects only in the particular database that houses that study—and that there's no risk of collision between role names. Start with the _README_ for the overall multitenancy scheme here:

- **[ysql-case-studies](https://github.com/YugabyteDB-Samples/ysql-case-studies/tree/main/)**

It takes only minutes to download the code and then run the scripts that install it all and test it all. As a bonus, you can install and run the code without making any changes in a Vanilla [PostgreSQL](https://www.yugabyte.com/postgresql/) cluster to demonstrate [the full compatibility between PostgreSQL and YSQL](https://www.yugabyte.com/postgresql/postgresql-compatibility/). It has been tested using both Version 11 (upon which the current latest YSQL implementation is based) and the _current_ PostgreSQL version.

## Creating, altering, and dropping subprograms

These are the relevant SQL statements:

- _[create [or replace] function](../the-sql-language/statements/ddl_create_function/)_
- _[alter function](../the-sql-language/statements/ddl_alter_function/)_
- _[create [or replace] procedure](../the-sql-language/statements/ddl_create_procedure/)_
- _[alter procedure](../the-sql-language/statements/ddl_alter_procedure/)_
- _[drop function](../the-sql-language/statements/ddl_drop_function/)_
- _[drop procedure](../the-sql-language/statements/ddl_drop_procedure/)_
