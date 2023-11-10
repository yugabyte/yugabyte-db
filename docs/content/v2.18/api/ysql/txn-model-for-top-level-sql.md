---
title: Transaction model for top-level SQL statements [YSQL]
headerTitle: The transaction model for top-level SQL statements
linkTitle: Transaction model for top-level SQL statements
description: Explains how top-level SQL statements are executed each in its own transaction unless explicitly started with "begin;" or "start transaction;" [YSQL].
menu:
  stable:
    identifier: txn-model-for-top-level-sql
    parent: api-ysql
    weight: 20
type: docs
---

This section describes how top-level SQL statements, sent to the PostgreSQL server using TCP/IP, execute. The same model, of course, applies for how YugabyteDB's YSQL subsystem  executes such statements. The chapter [Frontend/Backend Protocol](https://www.postgresql.org/docs/11/protocol.html) in the PostgreSQL documentation describes how the server processes, and responds to, SQL statements that are sent this way. 

## Client-side libraries and tools

Various client-side libraries are available to allow you to send and receive requests that use the low-level protocol from an ordinary programming language—like, for example:
- [libpq — C Library](https://www.postgresql.org/docs/current/libpq.html) (documented as part of the overall PostgreSQL documentation set);
- [pgJDBC — the PostgreSQL JDBC Driver](https://jdbc.postgresql.org/) from _postgresql.org_;
- [psycopg — for Python](https://www.psycopg.org/);
- and, of course, [_psql_](https://www.postgresql.org/docs/11/app-psql.html), the so-called PostgreSQL interactive terminal, together with the YugabyteDB equivalent, [_ysqlsh_](../../../admin/ysqlsh/).

There are many others.

However, because each of these client libraries and tools adds its own notion called "autocommit" (or a similar term) as a purely client-side construct, it's best to understand the transaction model first just in terms of the server's response to the TCP/IP protocol as you'd see it (as a thought experiment) if you chose your own favorite low-level programming language to write a tool similar to _psql_ to send and receive the messages that the protocol specifies. This way, you'd observe the native, unadulterated, server behavior.

## Transaction control statements

PostgreSQL and YSQL support these transaction control SQL statements:

- [_begin_](../the-sql-language/statements/txn_begin/) (alternatively spelled [_start transaction_](../the-sql-language/statements/txn_start/))
- [_commit_](../the-sql-language/statements/txn_commit/) (alternatively spelled [_end_](../the-sql-language/statements/txn_end/))
- [_rollback_](../the-sql-language/statements/txn_rollback/) (alternatively spelled [_abort_](../the-sql-language/statements/txn_abort/))

The implication is that a session must always be in one of two possible states:

- A transaction is ongoing.
- No transaction is ongoing.

Notice that _commit_ (or _end_) and _rollback_ (or _abort_) are meaningful only when the session has an ongoing transaction. If you issue one of these while there is no ongoing transaction, it has no effect and causes the _25P01_ (_no_active_sql_transaction_) error/warning. (The error/warning status depends on the value of the session's _client_min_messages_ setting.) Similarly, _begin_ (or _start transaction_) are meaningful only when there is no ongoing transaction. If you issue one of these while a transaction is already ongoing, then you get the _25001_ (_active_sql_transaction_) error/warning. Notice, too, that _commit_ (or _end_) have the same effect as _rollback_ (or _abort_) if an ongoing transaction has suffered an error. In this state, any SQL statement apart from _rollback_ (or _abort_) or _commit_ (or _end_) causes the _25P02_ (_in_failed_sql_transaction_) error.

(There are also transaction control statements to set and rollback to savepoints; and to set the mode of an ongoing transaction—to _read committed_, _repeatable read_, or _serializable_. These can be used only during an ongoing transaction. They are not interesting in the present discussion.)

## Semantics of issuing non-transaction-control SQL statements during an ongoing transaction

The effects of ordinary non-transaction-control SQL statements that are issued during an ongoing transaction are essentially private to the current session and invisible to any other concurrent sessions. If the current session issues _rollback_, then from the point of view of other sessions, it's the same as if the current session had done nothing. Only if the current session issues _commit_ will the effects of its transaction become visible in other concurrent sessions. 

{{< tip title="See the dedicated YSQL documentation section on isolation levels." >}}
The behavior of concurrent sessions, each of which has an ongoing transaction, needs very careful description—and it depends critically on which so-called _isolation level_ was specified when each transaction was started. See the section [Isolation levels](../../../explore/transactions/isolation-levels/). The isolation level of each session affects, for example:

- how sessions might wait on each other in contention scenarios;
- what kinds of conflict errors might occur, and when they occur;
- what one session sees, during its ongoing transaction, when another concurrent session issues _commit_.
{{< /tip >}}

The pedagogy that this page addresses does not depend on understanding the semantics of isolation levels.

## Semantics of issuing non-transaction-control SQL statements when no transaction is ongoing

Different RDBMSs behave differently in this scenario. Of course, YugabyteDB's YSQL behaves identically to PostgreSQL—and only this behavior will be described here.

The server implicitly, and automatically, starts a transaction (using the session's current value of the _default_transaction_isolation_ run-time parameter). Then it executes the current non-transaction-control SQL statement. And then it implicitly and automatically issues _commit_. (As mentioned, this will have the same effect as _rollback_ if the current non-transaction-control SQL statement caused an error.) This leaves the session back in its starting state with no transaction ongoing.

Notice that this behavior is defined and implemented entirely server-side. The PostgreSQL documentation does not describe this mode explicitly and does not, therefore, name the mode. (It describes only the mode where you start and end a multi-statement transaction explicitly. But it doesn't name this mode either.) The YSQL documentation coins these two terms of art for the two modes:

- single statement automatic transaction mode; _or_
- multistatement manual transaction mode.

Critically, you choose between these modes simply by issuing, or not issuing, _begin_ (or _start transaction_) when there is currently no ongoing transaction. And you do this simply by submitting the appropriate SQL statements (in the context of the thought experiment that set the stage for the present discussion) by using your own program that sends the SQL statements using TCP/IP and that receives the responses over the same protocol.

{{< tip title="How to detect whether or not a transaction is ongoing." >}}
There is no dedicated built-in SQL function for this purpose. Nor is it possible to write a deterministically reliable user-defined boolean function, say _in_ongoing_txn()_, to do this because, if no transaction were ongoing at the moment that you issue _select txn_is_ongoing()_, then this statement would anyway execute in single statement automatic transaction mode so that, tautologically, the function could only be deemed correct if it always returned _true_—and this would render it useless. Of course, by extension of this thinking, a built-in SQL function would suffer in the identical way.

Here's a reasonably reliable expedient approach. It relies on the inherent uncertainty of reading the time from a clock. Simply do this:

```plpgsql
select (statement_timestamp() > transaction_timestamp())::text as txn_is_ongoing;
```

You hope, here, to rely on the fact that the _timestamptz_ value at which the _select_ statement starts will be identical, within measurement limits, to the _timestamptz_ value at which the automatically started enclosing single statement transaction began. And, indeed, you're very likely to see that this _does_ reliably return _false_. On the other hand, you're very likely to see that this:

```plpgsql
begin;
select (statement_timestamp() > transaction_timestamp())::text as txn_is_ongoing;
```

reliably returns _true_ because the typical client-server round trip time for the _begin_ statement is long enough for the clock measurements to detect. You can subvert it thus:

```plphsql
begin\;
select (statement_timestamp() > transaction_timestamp())::text as txn_is_ongoing;
```

The `\;` _psql_ locution asks to send the two SQL statements that it separates in a single round-trip so that you're very likely to see that this always, _but wrongly_, returns _false_.

Notice that you can, by hand, ensure that _client_min_messages_ is set to a severity lower than, or equal to, _warning_ and issue _begin_. If this completes silently, then you know that you were _not_ in an ongoing transaction (and that you _are_ in one now). And if it causes the _25001_ warning,  then you know that you _were_ in an ongoing transaction (and that you _are still_ in one now). But it's impossible to encapsulate this manual approach into a function.
{{< /tip >}}

## Observing single statement automatic transaction mode by setting « log_statement = 'all' »

{{< tip title="It's best to use vanilla PostgreSQL for this experiment." >}}
It turns out that the PostgreSQL server log is uncluttered and easy to read. In particular, transaction control statements are logged using the ordinary SQL text of the statement, just like non-transaction-control statements are. For various reasons, the YugabyteDB YSQL server log, even on a single-node cluster, is very cluttered and transaction control statements are logged in an idiosyncratic way. It isn't necessary to explain why the log formats differ. Its sufficient to say simply that PostgreSQL and YugabyteDB YSQL behave identically with respect to the semantic notions that this page explains; and that it's better to observe the less cluttered server log.
{{< /tip >}}

Observe the log when you issue, say, this _insert_ when no transaction is ongoing:

```psql
insert into s.t(v) values(42);
```

The log shows exactly and only this statement—in other words, you don't see _begin_ before the _insert_ statement and you don't see _commit_ after it. You must simply understand, from this documentation, that the server executes the code that implements _begin_ (taking account of the current _default_transaction_isolation_ setting) before the _insert_ statement, that you do see in the log, executes. And then it executes the code that implements _commit_ (turning this into _rollback_ if the _insert_ caused an error) when the _insert_ statement finishes.

## Observing multistatement manual transaction mode by setting « log_statement = 'all' »

Observe the log when you issue, say, this, again starting when no transaction is ongoing:

```plpgsql
begin;
insert into s.t(v) values(42;
commit;
```

Here, the log shows these three statements exactly as you entered them.

## What is the so-called "autocommit mode" that client-side libraries and tools implement?

This is most definitely an invented, client-side-only, notion that has no direct server implementation. Most PostgreSQL experts consider it to be nothing more than entirely unnecessary and potentially hugely confusing and dangerous. See the Cybertech post [Disabling autocommit in PostgreSQL can damage your health](https://www.cybertec-postgresql.com/en/disabling-autocommit-in-postgresql-can-damage-your-health/) by Laurenz Albe. (Here, Laurenz uses "autocommit mode" to denote what, above, was denoted by the term of art "single statement automatic transaction mode". And he emphasizes this:

> PostgreSQL operates in autocommit mode, and there is no way to change that behavior on the server side.

However, it seems that all client-side libraries and tools have implemented a notion that they _call_ "autocommit". For example, _libpq_ has a so-called embedded SQL command thus:

```plpgsql
SET AUTOCOMMIT { = | TO } { ON | OFF }
```

JDBC has this:

```plpgsql
java.sql.Connection.setAutoCommit(boolean)
```

_psycopg2_ has this:

```plpgsql
connection.set_session(autocommit=True)
```

and _psql_ (and therefore _ysqlsh_) have the meta-command:

```plpgsql
\set AUTOCOMMIT { 'on' | 'off' }
```

The [PostgreSQL documentation for _psql_](https://www.postgresql.org/docs/11/app-psql.html) describes it thus:

> The autocommit-off mode works by issuing an implicit BEGIN for you, just before any command that is not already in a transaction block and is not itself a BEGIN or other transaction-control command...

You can see this client-side implementation by observing the server log. Thereafter, it's just the same as if you had issued the _begin_ with _autocommit_ set to _off_ using your own SQL—so it's up to you eventually to issue _commit_ or _rollback_ to end the transaction.

<a name="avoid-client-side-autocommit"></a>
{{< tip title="Simply avoid using any client-side 'autocommit' feature." >}}
Yugabyte recommends that you adopt the practice that the Cybertech post by Laurentz Albe recommends: use the native PostgreSQL behavior "as is"; and choose between _single statement automatic transaction mode_ and _multistatement manual transaction mode_ explicitly by whether or not you issue _begin_ explicitly before the non-transaction-control statement(s) that you want to execute within a single enclosing transaction.

Notice that you can achieve this implicitly by always implementing every multistatement transaction in a user-defined subprogram. See the section [Restrictions that govern the use of "commit" in user-defined subprograms](../user-defined-subprograms-and-anon-blocks/#restrictions-that-govern-the-use-of-commit-in-user-defined-subprograms) and in particular the tip [Avoid invoking user-defined subprograms in multistatement manual transaction mode](../user-defined-subprograms-and-anon-blocks/#avoid-invoking-user-defined-subprograms-in-multistatement-manual-txn-mode).
{{< /tip >}}