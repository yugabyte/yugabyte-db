---
title: DO statement [YSQL]
headerTitle: DO
linkTitle: DO
description: Describes how to use the DO statement to execute an anonymous PL/pgSQL block statement—in other words, a transient anonymous PL/pgSQL procedure.
menu:
  stable:
    identifier: cmd_do
    parent: statements
type: docs
---

## Synopsis

Use the `DO` statement to execute an anonymous PL/pgSQL block statement—in other words, a transient anonymous PL/pgSQL procedure. The [plpgsql_block_stmt](../../../syntax_resources/grammar_diagrams/#plpgsql-block-stmt) is treated as though it were the body of a procedure with no parameters:

- Any SQL statement that the block statement encounters is treated in the same way as when it is encountered in a _[language plpgsql](../../../user-defined-subprograms-and-anon-blocks/language-plpgsql-subprograms/)_ subprogram so that if a `DO` statement is repeatedly executed in the same session, using the textually identical block statement, then the second and subsequent executions of the contained SQL statement benefit from the syntax and semantics analysis that was done when it was first encountered.

The grammar allows an optional `LANGUAGE` clause that can be written either before or after the code block. However, the only supported choice is _language plpgsql_. For example, an attempt to execute a `DO` statement that specified _language sql_ causes the _0A000_ run-time error:

```output
language "sql" does not support inline code execution
```

See the thread on the _[pgsql-general](mailto:pgsql-general@lists.postgresql.org)_ email list, _[Why can't I have a "language sql" anonymous block?](https://www.postgresql.org/message-id/C9838A29-8C84-4F68-9C41-5CB4665911E5@yugabyte.com)_

{{< tip title="Avoid using the optional 'LANGUAGE' clause in a 'DO' statement." >}}
Specifying _language plpgsql_ brings no benefit with respect to omitting the `LANGUAGE` clause altogether. You can assume that all developers know that the only supported implementation language is PL/pgSQL. Yugabyte therefore recommends that you avoid cluttering your code and simply always omit the optional `LANGUAGE` clause.
{{< /tip >}}


## Syntax

{{< note title="The syntax diagram omits the optional 'LANGUAGE' clause." >}}
The syntax diagram respects the advice that the tip _Avoid using the optional 'LANGUAGE' clause in a 'DO' statement_, above, gives.
{{< /note >}}

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
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/do.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade show active" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/the-sql-language/statements/do.diagram.md" %}}
  </div>
</div>

## Semantics

### *plpgsql_block_stmt*
The procedural language code to be executed, [plpgsql_block_stmt](../../../syntax_resources/grammar_diagrams/#plpgsql-block-stmt). This must be specified as a string literal, just as in [`CREATE FUNCTION`](../ddl_create_function) and  [`CREATE PROCEDURE`](../ddl_create_procedure). Yugabyte recommends that you use dollar-quoting and standardize on, for example, _$body$_.

### *lang_name*
Specifies the name of the procedural language that the code is written in. The default is `plpgsql`. See the tip _Avoid using the optional 'LANGUAGE' clause in a 'DO' statement_, above. This code is legal. And it runs without error and has the intended effect.

```plpgsql
do
  language plpgsql
$body$
begin
  raise info 'Block statement started at %',
    to_char((statement_timestamp() at time zone 'UTC'), 'hh24:mi:ss Dy');
end;
$body$;
```

And the effect is identical if _language plpgsql_ is omitted.

## Notes

See the section [Issuing "commit" in user-defined subprograms and anonymous blocks](../../../user-defined-subprograms-and-anon-blocks/commit-in-user-defined-subprograms/).

## Example

```plpgsql
do $body$
begin
  drop schema if exists s cascade;
  create schema s;

  create table s.masters(
    mk serial primary key,
    mv text not null unique);

  create table s.details(
    dk serial primary key,
    mk int not null references s.masters(mk),
    dv text not null);
end;
$body$;
```

Suppose that at the moment the `DO` statement is executed, the schema _s_ already exists but is owned by a user other than what the _current_role_ built-in function returns (and that this current role is not a superuser). Assume, too, that there is currently no ongoing transaction so that the block statement is executed in _single statement automatic transaction mode_ (see the section [Semantics of issuing non-transaction-control SQL statements when no transaction is ongoing](../../../txn-model-for-top-level-sql/#semantics-of-issuing-non-transaction-control-sql-statements-when-no-transaction-is-ongoing).)

 The _drop schema if exists s cascade_ attempt will cause the _42501_ error:

```output
must be owner of schema s
```

The block will then exit immediately with an unhandled exception and the run-time system will automatically issue an under-the-covers _commit_—which will have the same effect, here, as _rollback_. Compare this behavior with that of encapsulating the same statements in an explicit _start transaction; ... commit;_ encapsulation to use _multistatement manual transaction mode_:

```plpgsql
start transaction;
  drop schema if exists s cascade;
  create schema s;

  create table s.masters(
    mk serial primary key,
    mv text not null unique);

  create table s.details(
    dk serial primary key,
    mk int not null references s.masters(mk),
    dv text not null);
commit;
```

Now four errors are reported: first, the _42501_ error is reported, just as when the `DO` statement is executed in _single statement automatic transaction mode_; but then three occurrences of the _25P02_ error (_current transaction is aborted, commands ignored until end of transaction block_) are reported.

The `DO` statement approach therefore provides the better encapsulation for the four-statement implementation of the business requirement than does the _start transaction; ... commit;_ approach.
