---
title: The "raise" statement [YSQL]
headerTitle: The "raise" statement
linkTitle: >
  The "raise" statement
description: Describes the syntax and semantics of the PL/pgSQL "raise" statement. [YSQL].
menu:
  preview:
    identifier: raise
    parent: basic-statements
    weight: 30
type: docs
showRightNav: true
---

## Syntax

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
  {{% includeMarkdown "../../../../../syntax_resources/user-defined-subprograms-and-anon-blocks/language-plpgsql-subprograms/plpgsql-syntax-and-semantics/executable-section/basic-statements/plpgsql_raise_stmt,plpgsql_raise_level,plpgsql_raise_shortcut_for_exception_or_message,plpgsql_raise_using_item.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade show active" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../../../../syntax_resources/user-defined-subprograms-and-anon-blocks/language-plpgsql-subprograms/plpgsql-syntax-and-semantics/executable-section/basic-statements/plpgsql_raise_stmt,plpgsql_raise_level,plpgsql_raise_shortcut_for_exception_or_message,plpgsql_raise_using_item.diagram.md" %}}
  </div>
</div>

## Semantics
The PL/pgSQL _raise_ statement has three distinct purposes:

- _First_, to re-raise the exception that the handler caught (legal only in an exception handler).
- _Second_, to raise a specified exception and optionally to specify one, some, or all of the various texts that might annotate it:
  - the _main message_
  - the _hint_
  - the _detail_
  - the _schema_, _table_, _column_ and _data type_, where applicable, of the object upon which the erroring operation was attempted
  - the _constraint_, where applicable, that the erroring operation violated.

- _Third_, to render a message at the specified severity _level_ to _stderr_ or to the server log file.

The first purpose is met by the [bare _raise_](./#bare-raise) statement. The _[plpgsql_raise_level](./#plpgsql-raise-level)_ rule decides between the second purpose (when the _level_ is _exception_) and the third purpose (when the _level_ is any legal keyword except for _exception_).

With one restriction, you can set the same fields for the _raise_ rendered message as you can observe with the _get stacked diagnostics_ statement. The restriction is that you cannot set the exception context. This is because the context is determined programatically—and reflects information, like the call stack, that emerges only at run time. It wouldn't make sense, therefore, to think that the _raise_ statement might set this.

Notice that, though it is legal to set all possible kinds of annotation text for the third purpose, only the _main message_ makes sense.

The legal keywords that the _[plpgsql_raise_using_item](./#plpgsql-raise-using-item)_ rule specifies correspond, exactly one-to-one, to the legal keywords for the _[plpgsql_stacked_diagnostics_item_name](../../../exception-section/#plpgsql-stacked-diagnostics-item-name)_ rule. This extended version of the table that's included in the section [How to get information about the error](../../../exception-section/#diagnostics-item-names) shows the correspondence:

| **"Raise" using item** | **Diagnostics item name** | **Description**                                  |
| ---------------------- | ------------------------- | ------------------------------------------------ |
| errcode                | returned_sqlstate         | The code of the error that caused the exception. |
| message                | message_text              | The text of the exception's primary message.     |
| detail                 | pg_exception_detail       | The text of the exception's detail message.      |
| hint                   | pg_exception_hint         | The text of the exception's hint message.        |
| schema                 | schema_name               | The name of the schema related to exception.     |
| table                  | table_name                | The name of the table related to exception.      |
| column                 | column_name               | The name of the column related to exception.     |
| datatype               | pg_datatype_name          | The name of the data type related to exception.  |
| constraint             | constraint_name           | The name of the constraint related to exception. |

The message is rendered, optionally, in two ways:

- It is rendered, or not rendered, on _stderr_ on the node where the client program is running according to the _level_ used in the _raise_ statement together with the current value of the _client_min_messages_ run-time parameter.

- It is rendered, or not rendered, in the server log file according to the _level_ used in the _raise_ statement and the current value of the _log_min_messages_ configuration parameter.

Any two sessions can each set different values for each of the two _\*min_messages_  parameters. The outcome is easy to understand when the report text is rendered on _stderr_ where the client application runs. But it takes more effort to understand, for two reasons, when  the report text is rendered on the server log file:

- Messages from different concurrent clients simply interleave in the single  server log file on a particular node.
- YugabyteDB is typically deployed using three or more nodes—and, especially when a load balancer is used, it might not be easy to identify which node is hosting the Postgres server process for the client of interest.

The discussion in this section will therefore be limited to the messages that are rendered on _stderr_ and that are controlled by the _client_min_messages_ parameter.

### How to specify whether or not the "raise" report text is rendered on "stderr"

You can see the allowed values for _client_min_messages_ by attempting to set it to, say, _illegal_. You get an error, of course. And the _hint_ lists the available values. However, empirical testing shows that the list of values is wrong in that it omits _info_ and bare _debug_. This:

```plpgsql
set client_min_messages = info;
```

and this:

```plpgsql
set client_min_messages = debug;
```

each runs without error. For completeness, the missing _info_ is added at the end of the list here—and the missing bare _debug_ is added at the start. The order of the values in the list is significant. Here it is:

- _debug_, _debug5_, _debug4_, _debug3_, _debug2_, _debug1_, _log_, _notice_, _warning_, _error_, _info_

Notice how the spellings of the allowed values correspond, but only approximately, to the list of legal _level_ keywords for the _raise_ statement.

- The value _error_ corresponds to the _raise_ statement's _level_ keyword _exception_.
- The values (bare) _debug_, _debug1_, _debug2_, _debug3_, _debug4_, and _debug5_ all correspond to the _raise_ statement's _level_ keyword _debug_.

These differences reflect the fact that the internal implementation of top-level SQL statements can also generate messages—and that the SQL message levels are more nuanced than for those that the PL/pgSQL _raise_ statement can specify.

The parameter name, _client_min_messages_, includes an abbreviation for "minimum"—and this implies that it's appropriate to say that the allowed values are listed in order of increasing priority where (bare) _debug_ has the lowest priority. (The allowed values _info_ and _error_ tie with each other; each has the highest priority.) Here is the rule that determines whether the report text that the _raise_ statement specifies is rendered or not:

- A message whose _level_ corresponds to the current setting of _client_min_messages_, or to any allowed value that has a higher priority than the current setting, will be rendered.
- Conversely, a message whose _level_ corresponds to any allowed value that has a lower priority than the current setting of _client_min_messages_, will not be rendered.
- As a special case, messages with the level _exception_ or _info_ are _always_ rendered. (This is why _info_ is written, above, at the end of the list.)

{{< tip title="Don't specify the levels 'debug' or 'log' in the 'raise' statement." >}}
The _log_ level is lower than the _notice_ level. The report text from _raise log_ will,  therefore, not be rendered unless _client_min_messages_ is set to _log_ (or to any one of the levels from (bare) _debug_, _debug1_, and so on through _debug5_). But if you set _client_min_messages_ to _log_, then you'll see many message texts that the internal implementation of top-level SQL statements generates. Try this example:

```plpgsql
set client_min_messages = warning;

do $body$
begin
  raise log '"drop table if exists" invoked';
  drop table if exists pg_temp.t;
  raise log '"create table" invoked';
  create table pg_temp.t(n int);
  raise log '"drop table" invoked';
  drop table pg_temp.t;
end;
$body$;
```
As expected, it completes silently. Now set _client_min_messages_ to _log_ and repeat the _do_ statement. Now you see this on _stderr_:

```output
LOG:  "drop table if exists" invoked
NOTICE:  table "t" does not exist, skipping
LOG:  "create table" invoked
LOG:  "drop table" invoked
```

Notice that, in addition to the three intended _log_ level message texts, a report text at _notice_ level, from the internal implementation, is rendered  thus: _table "t" does not exist, skipping_. This is because the _notice_ level has higher priority than the _log_ level.

The intended benefit of the _log_ level is that you can permanently include tracing output in your code. Such tracing text can, when appropriate, render locally available values. Then you can turn the trace output on or off at run-time on a per-session basis. However, the trace output might become be so cluttered that it will be effectively unreadable.

You get significantly cluttered outcome when you specify the _debug_ level in the _raise_ statement. Try this:

```plpgsql
set client_min_messages = debug;
```

and repeat the _do_ same statement. Now you see this on _stderr_:

```output
DEBUG:  StartTransaction(1) name: unnamed; blockState: DEFAULT; state: INPROGRESS, ybDataSent: N, ybDataSentForCurrQuery: N, xid/subid/cid: 0/1/0
LOG:  "drop table if exists" invoked
DEBUG:  Resetting read point for statement in Read Committed txn
DEBUG:  relation "pg_temp.t" does not exist
NOTICE:  table "t" does not exist, skipping
LOG:  "create table" invoked
DEBUG:  Resetting read point for statement in Read Committed txn
LOG:  "drop table" invoked
DEBUG:  Resetting read point for statement in Read Committed txn
DEBUG:  drop auto-cascades to type t
DEBUG:  drop auto-cascades to type t[]
DEBUG:  CommitTransaction(1) name: unnamed; blockState: STARTED; state: INPROGRESS, ybDataSent: Y, ybDataSentForCurrQuery: Y, xid/subid/cid: 0/1/4
```

You can implement a simple dynamically controllable scheme to render clean  tracing like this:

```plpgsql
\c :db :u
drop schema if exists utils cascade;
create schema utils;

create procedure utils.trace(msg in text)
  language plpgsql
  set search_path = pg_catalog, pg_temp
as $body$
declare
  trace_on boolean not null := false;
begin
  <<evaluate_trace_on>>begin
    trace_on = lower(current_setting('trace.mode')) = 'true';
  exception when undefined_object then
    declare
      err_msg text not null := '';
    begin
      get stacked diagnostics err_msg := message_text;
      assert err_msg = 'unrecognized configuration parameter "trace.mode"';
    end;
  end evaluate_trace_on;
  if trace_on then
    raise info '%', msg;
  end if;
end;
$body$;
```

There's more code than you might, at first, have expected because this:

```plpgsql
trace_on = lower(current_setting('trace.mode')) = 'true';
```
will cause an error if the user-defined run-time parameter _trace.mode_ doesn't exist. No such parameters exist at session start. One is implicitly created (with just session-duration) when a value is first assigned to it. By definition, here, when the value is _true_ this means that tracing is requested in the present session. It's natural to define that non-existence of _trace.mode_, along with any value other than _true_ that it might have, means that tracing is not requested in the present session. It's good practice, when you handle an error in order simply to interpret this as a benign expected outcome and to suppress it, to check that your reasoning is sound. That's why the handler goes on to check not just the error code but also that the primary message text is exactly what is expected.

{{< note title="Procedure 'utils.trace()' nicely illustrates the inevitability of expected errors" >}}
The term "expected error" tautologically captures the notion that some errors are simply bound to occur in some scenarios—and that such an occurrence can be benign. The Procedure _utils.trace()_ perfectly illustrates the case where sometimes ordinary program actions must be implemented in an exception handler. A block statement with an exception section with such a handler will almost always be an inner block within the block statement that implements the main program logic.
{{< /note >}}

Here's how to use the _utils.trace()_ procedure:

```plpgsql
drop schema if exists s cascade;
create schema s;

create procedure s.p1()
  language plpgsql
  set search_path = pg_catalog, pg_temp
as $body$
begin
  call utils.trace('"drop table if exists" invoked');
  drop table if exists pg_temp.t;
  call utils.trace('"create table" invoked');
  create table pg_temp.t(n int);
  call utils.trace('"drop table" invoked');
  drop table pg_temp.t;
end;
$body$;
```

Test it when the user-defined run-time parameter _trace.mode_ doesn't yet exist:

```plpgsql
\c :db :u
set client_min_messages = warning;
call s.p1();
```

This finishes silently because tracing isn't yet turned on. Now turn it on and test again:

```plpgsql
set trace.mode = true;
call s.p1();
```

This output is rendered on _stderr_:

```output
INFO:  "drop table if exists" invoked
INFO:  "create table" invoked
INFO:  "drop table" invoked
```

Now turn tracing off and test again:

```plpgsql
set trace.mode = 42;
call s.p1();
```

This finishes silently, as intended. (All user-defined run-time parameter settings are observed as _text_ values—and so _42_ has the same effect as _false_.)

It's easy, now, to create a second two-argument overload of _util.trace()_ that adds a _kind_ argument to give more granular control over what is traced and what isn't.

```plpgsql
create procedure utils.trace(kind in text, msg in text)
  language plpgsql
  set search_path = pg_catalog, pg_temp
as $body$
declare
  param_val text not null := '';
  options text[] not null := array[''];
begin
  begin
    param_val := lower(current_setting('trace.kinds'));
    options := string_to_array(replace(param_val, ' ', ''), ',');
  exception when undefined_object then
    declare
      err_msg text not null := '';
    begin
      get stacked diagnostics err_msg := message_text;
      assert err_msg = 'unrecognized configuration parameter "trace.kinds"';
    end;
  end;

  if array[lower(kind)] <@ options then
    raise info '%', msg;
  end if;
end;
$body$;
```

The _utils.trace()_ procedure uses native array functionality to transform a _text_ value like _'a, b,  c, d'_ into (in this example) the _text[]_ array _array['a', 'b', 'c', 'd']_. And it uses the _contains_ operator, _<@_, to test if a value of interest is among the array's elements.

Here's how to use the two-argument overload of the _utils.trace()_ procedure:

```plpgsql
create procedure s.p2()
  language plpgsql
  set search_path = pg_catalog, pg_temp
as $body$
begin
  call utils.trace('red',    'Red message.');
  call utils.trace('blue',   'Blue message.');
  call utils.trace('green',  'Green message.');
  call utils.trace('yellow', 'Yellow message.');
end;
$body$;
```

Test it when the user-defined run-time parameter _trace.kinds_ doesn't yet exist:

```plpgsql
\c :db :u
set client_min_messages = warning;
call s.p2();
```

This finishes silently because tracing isn't yet turned on. Now request just the _blue_ kind of trace output and test again:

```plpgsql
set trace.kinds = 'blue';
call s.p2();
```

This output is rendered on _stderr_:

```output
INFO:  Blue message.
```

Now request just the _green_ kind of trace output (testing the intended case-insensitivity) and test again:

```plpgsql
set trace.kinds = 'Green';
call s.p2();
```

This output is rendered on _stderr_:

```output
INFO:  Green message.
```

Now request the _red_, the _blue_, and the _yellow_ kinds of trace output and test again:


```plpgsql
set trace.kinds = 'red,  BLUE,  Yellow';
call s.p2();
```

This output is rendered on _stderr_:

```output
INFO:  Red message.
INFO:  Blue message.
INFO:  Yellow message.
```

Finally, turn off all kinds of trace output and test again:

```plpgsql
set trace.kinds = '';
call s.p2();
```

This finishes silently, as intended.
{{< /tip >}}

### The semantically legal variants of the "raise" statement

The _raise_ statement syntax is defined by the mandatory _raise_ keyword followed by _three_ optional clauses:

- _[plpgsql_raise_level](./#plpgsql-raise-level)_ — hereinafter the _level_ clause
- _[plpgsql_raise_shortcut_for_exception_or_message](./#plpgsql-raise-shortcut-for-exception-or-message)_ — hereinafter the _shortcut_ clause
- _USING [plpgsql_raise_using_item](./#plpgsql-raise-using-item) [ , ... ]_ — hereinafter the _using_ clause

However, semantics rules govern which combinations of these clauses are legal. These are the useful legal combinations:

```output
RAISE;

RAISE [LEVEL] SQLSTATE errcode;
RAISE [LEVEL] exception_name;
RAISE [LEVEL] message_literal [ text_expression [ , ... ] ];

RAISE [LEVEL] USING option { := | = } text_expression [ , ... ];
```

Notice that this:

```plpgsql
do $body$
begin
  raise exception;
end;
$body$;
```

with any legal _level_ keyword, causes the _42601_ syntax error.

The bare _raise_ variant is a singleton. In all the other variants, _level_ is optional. Omitting it has the same effect as writing _exception_.

{{< tip title="Always specify the 'exception' level explicitly when you want this level" >}}
Only when you want to specify the _exception_ level can you omit that keyword. The effort to type just nine characters is trivial. And the readability of your code is significantly improved by specifying _exception_ explicitly.
{{< /tip >}}

These combinations are also legal:

```output
RAISE [LEVEL] SQLSTATE errcode USING option { := | = } text_expression [ , ... ];
RAISE [LEVEL] exception_name USING option { := | = } text_expression [ , ... ];
RAISE [LEVEL] message_literal [ text_expression [ , ... ] ] USING option { := | = } text_expression [ , ... ];
```

However, these hybrids bring the possibility of specifying the same _using_ option twice. (Notice that the same notion, for historical reasons, has different spellings at different syntax spots. For example, _sqlstate_ and _errcode_ denote the same notion.) Try these three negative tests. First:

```plpgsql
do $body$
begin
  raise exception sqlstate 'YB257' using errcode := 'YB257';
end;
$body$;
```

This causes the _42601_ runtime error, _"RAISE option already specified: ERRCODE"_. Second:

```plpgsql
do $body$
begin
  raise exception unique_violation using errcode := '23505';
end;
$body$;
```

This causes the same _42601_ runtime error with the same message. And third:

```plpgsql
do $body$
begin
  raise info 'Some facts' using message := 'Some facts';
end;
$body$;
```

This, too, causes the _42601_ runtime error but the message here is _"42601: RAISE option already specified: MESSAGE"_.

{{< tip title="Don't use the hybrid 'raise' syntax option" >}}
The term "hybrid" is used here to denote the syntax that has both the _[shortcut](./#plpgsql-raise-shortcut-for-exception-or-message)_ clause and the _[using](./#plpgsql-raise-using-item)_ clause.

You might decide to adopt a simple rule never to use the _shortcut_ clause. This is a viable approach because the _using_ clause expresses a superset of the semantics that the _shortcut_ clause can express.

Alternatively, when you want to specify only _using_ options that the _shortcut_ clause _can_ express, you might prefer to use the _shortcut_ clause for such a case in order to make your code briefer.
{{< /tip >}}


#### "Bare raise"

This is the degenerate form of the syntax. Every optional clause is simply omitted, thus:

```plpgsql
raise;
```

It has a special meaning that distinguishes it from all of the other _raise_ variants. You can write it in either the executable section or the exception section without causing a syntax error. But try this:

```plpgsql
drop schema if exists s cascade;
create schema s;
create procedure s.p()
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  raise;
end;
$body$;

call s.p();
```

Procedure _s.p()_ is created without error. But you get the _0Z002_ runtime error:

```output
RAISE without parameters cannot be used outside an exception handler
```

This reflects the defined semantics of the bare _raise_ statement. When it's invoked in an exception handler, it re-raises the exception (together with the same stacked diagnostics facts) that was caught by the handler. Try this:

```plpgsql
drop schema if exists s cascade;
create schema s;
create function s.f()
  returns table(z text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  begin
    perform 1.0/0.0;
  exception when division_by_zero then
    z := '"division_by_zero" caught';                                 return next;
    raise;
  end;
exception when division_by_zero then
  declare
    v_pg_exception_context text not null := '';
  begin
    get stacked diagnostics
      v_pg_exception_context := pg_exception_context;

    z := '"division_by_zero" caught again, following "bare raise"';   return next;
    z := '';                                                          return next;
    z := 'context: '||v_pg_exception_context;                         return next;
  end;
end;
$body$;

select s.f();
```

This is the result:

```output
 "division_by_zero" caught
 "division_by_zero" caught again, following "bare raise"
 
 context: SQL statement "SELECT 1.0/0.0"                +
 PL/pgSQL function s.f() line 4 at PERFORM
```

The critical point, here, is that the exception context reports that the error occurred when executing _"perform 1.0/0.0;"_ in the function _"s.f()"_—and not at the bare _raise_ statement.

#### "raise info" with the "shortcut" syntax

First, try this counter-example:

```plpgsql
drop schema if exists s cascade;
create schema s;

create procedure s.p()
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
      raise info using
        errcode    := 'YB573',
        message    := 'Main info,',
        detail     := 'Info detail.',
        hint       := 'Info hint.',
        schema     := 'n/a',
        table      := 'n/a',
        column     := 'n/a',
        datatype   := 'n/a',
        constraint := 'n/a';
end;
$body$;

\set VERBOSITY default
call s.p();
```

It produces this output:

```output
INFO:  Main info,
DETAIL:  Info detail.
HINT:  Info hint.
```

You don't see the other annotation kinds unless you do this:

```plpgsql
\set VERBOSITY verbose
call s.p();
```

Now you see this output:

```output
INFO:  YB573: Main info,
DETAIL:  Info detail.
HINT:  Info hint.
SCHEMA NAME:  n/a
TABLE NAME:  n/a
COLUMN NAME:  n/a
DATATYPE NAME:  n/a
CONSTRAINT NAME:  n/a
LOCATION:  exec_stmt_raise, pl_exec.c:3852
```

It's certainly meaningless to specify the error code _YB573_ (even though you get what you asked for) because the code for successful completion of a top-level server call is _00000_. You might argue that the other annotation kinds could be useful if you're writing re-usable code for other developers, if there are some legal, but ill-advised uses, and if you want to issue a _warning_ when you detect such a case. (Notice that, by definition, only an _exception_ interrupts normal execution.) This general syntax variant:

```output
RAISE [LEVEL] USING option { := | = } text_expression [ , ... ];
```

gives you what you need for this use. However, the overwhelmingly common case for _raise_ with all legal options for _level_ except for _exception_ is to generate only the main message and to convey all of the facts that you intend in that single text. The general syntax then reduces to this:

```plpgsql
drop schema if exists s cascade;
create schema s;

create procedure s.p()
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  template constant text not null := 'Here are two interesting values: "%s"; and "%s".';
begin
  raise info using message := format(template, 'red', 'blue');
end;
$body$;

\set VERBOSITY default
call s.p();
```

The code takes advantage of the fact that the argument for each _using_ clause option is a text expression. It produces this output:

```output
INFO:  Here are two interesting values: "red"; and "blue".
```

Compare the executable section with the version that uses the shortcut syntax:

```plpgsql
begin
  raise info 'Here are two interesting values: "%"; and "%".', 'red', 'blue';
end;
```

This is less cluttered than the general syntax. And, of course, it produces the identical output. (Notice, though, that the special form of the value substitution syntax is less expressive than the general substitution syntax that the _format()_ function supports.) Nevertheless, the shortcut syntax is typically preferred for the _raise info_ use-case.

#### "raise exception" with the "shortcut" syntax

Consider a system of user-defined subprograms where the call stack can grow to a significant depth and where a subprogram can suffer an expected, but regrettable, error at the deepest level in the stack. Suppose that the error can be detected only in that deeply-nested subprogram invocation. Suppose, further, that an alternative viable approach that calls a different starting subprogram from the top-of-stack subprogram is available.

Of course it's possible to ensure that the design of every subprogram can notice such an outcome, abort its own execution, and communicate the failure to its callee using dedicated formal _status_ arguments—all the way up the stack. But this approach leads to cluttered code and it's hard to follow the paradigm consistently. Some languages have a native feature called _long jump_, or similar which would be preferred in the present scenario. But PL/pgSQL has no such feature. However raising an exception down in the depth and handling it at top level provides a sound alternative solution. The only requirement is that a dedicated handler can be written for just this scenario—and only this.

The following code provides a minimal, but sufficient, illustration of this approach. First, create the procedure that implements the preferred approach but that might fail in a predictable way.

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;

create procedure s.preferred(cause_preferred_failure in boolean)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  case cause_preferred_failure
    when true then
      raise info 'Regrettable but not unexpected: preferred() failed.';
      raise sqlstate 'YB257';
    else
      raise info 'preferred() succeeded.';
  end case;
end;
$body$;
```

Look at the table of [Error Codes and Condition Names](https://www.postgresql.org/docs/11/errcodes-appendix.html#ERRCODES-TABLE) in the PostgreSQL documentation. It has about (but a little fewer than) 250 entries. An error code must be a mixture of exactly five digits or upper-case Latin characters. (Try _raise sqlstate '93a57'_. It causes the _42601_ syntax  error: _"invalid SQLSTATE code"_.) This means that there are (10 + 26)^5 distinct possible values. Subtracting 250 for the values that PostgreSQL reserves means that there are still more than 60 million available values. You'll find it easy, therefore, to invent new codes for your development shop and to maintain a list of these, each with its purpose. For example, search in the page for _YB257_. It isn't found.

Next create the fallback procedure. Assume that it's, for example, significantly slower than _preferred()_ but that any failure it might suffer is unexpected:

```plpgsql
create procedure s.fallback(cause_fallback_failure in boolean)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  case cause_fallback_failure
    when true then
      -- Simulate ordinary, but unexpected, SQL error
      raise division_by_zero;
    else
      raise info 'fallback() succeeded.';
  end case;
end;
$body$;
```

Finally, create the procedure _caller()_. This simulates the subprogram that the client application invokes. In the real code, the execution point will move through several subprograms as the call stack grows until it reaches the procedure _preferred()_:

```plpgsql
create procedure s.caller(
  cause_preferred_failure in boolean = false,
  cause_fallback_failure  in boolean = false)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  ctx text not null := '';
  err text not null := '';
begin
  call s.preferred(cause_preferred_failure);
exception
  when sqlstate 'YB257' then
    begin
      call s.fallback(cause_fallback_failure);
    exception
      when others then
        get stacked diagnostics err := returned_sqlstate, ctx := pg_exception_context; 
        raise info 'Unexpected error: %', err;
        raise info '%', chr(10)||ctx;
    end;
  when others then
    -- Here only when preferred() fails in an unexpected way.
    get stacked diagnostics err := returned_sqlstate, ctx := pg_exception_context; 
    raise info 'Unexpected error: %', err;
    raise info '%', chr(10)||ctx;
end;
$body$;
```

Notice how the procedure _fallback()_ is invoked in the handler for the user-defined error with the dedicated code _YB257_. Because it's conceivable that the _fallback()_ might fail—even though analysis shows that it shouldn't, a _when others_ handler must be provided for this possibility. This implies using a nested block statement to implement the _when sqlstate 'YB257'_ handler. In this simulation, it simply reports the outcome with _raise info_. But in a real application, this would be signalled to the client as a failure. One way to do this is always to return an _outcome_ text that represents a JSON value. The flexibility of JSON allows the value to encode:

- EITHER _"success"_ together with any values that such an outcome might be specified to encode
- OR _"failure"_ together with all the diagnostic facts about the error that are deemed to be useful.

An alternative approach is to record the facts about the failure in a dedicated _incidents_ table with a self-populating _ticket_id_ primary key—and then to return the value of the _ticket_id_ along with the _failure_ status. A self-contained, downloadable implementation of this approach is implemented in the _[ysql-case-studies](https://github.com/YugabyteDB-Samples/ysql-case-studies)_ repo as the so-called _[hard-shell](https://github.com/YugabyteDB-Samples/ysql-case-studies/tree/main/hard-shell#hard-shell)_ case study. Look at the implementations of the procedures _[api.insert_master_and_details()](https://github.com/YugabyteDB-Samples/ysql-case-studies/blob/main/hard-shell/60-install-api.sql#L28)_ and _[support.insert_incident()](https://github.com/YugabyteDB-Samples/ysql-case-studies/blob/main/hard-shell/22-install-support.sql#L42)_.

Notice, finally, that the procedure _preferred()_ might too encounter an unexpected error that analysis failed to foresee. This explains the textually identical implementation of the inner block statement's  _when others_ handler in the outer block too. The code is uncomfortably repetitive. But the _get stacked diagnostics_ statement can be used only within the handler where the facts about the caught exception are to be observed.

Now run the demo first by simulating the outcome that's expected to be common: the procedure _preferred()_ simply succeeds.

```plpgsql
call s.caller();
```

This is the outcome:

```outcome
INFO:  preferred() succeeded.
```

Next, simulate the outcome where _preferred()_ fails and where _fallback()_ succeeds.

```plpgsql
call s.caller(cause_preferred_failure=>true);
```

This is the outcome:

```outcome
INFO:  Regrettable but not unexpected: preferred() failed.
INFO:  fallback() succeeded.
```

Finally, simulate the outcome where _preferred()_ fails (as is expected that it can) and where then _fallback()_ fails unexpectedly.

```plpgsql
call s.caller(cause_preferred_failure=>true, cause_fallback_failure=>true);
```

This is the outcome:

```outcome
INFO:  Regrettable but not unexpected: preferred() failed.
INFO:  Unexpected error: 22012
INFO:  
PL/pgSQL function s.fallback(boolean) line 6 at RAISE
SQL statement "CALL s.fallback(cause_fallback_failure)"
PL/pgSQL function s.caller(boolean,boolean) line 10 at CALL
```

{{< tip title="Avoid the 'raise <exception name>' syntax" >}}
This section has explained how raising a _user-defined_ exception, identified by an error code that PostgreSQL has not reserved, can be useful for implementing _long jump_-like functionality. The approach relies on the fact that an error code is not checked during syntax analysis against those that are listed in the table of [Error Codes and Condition Names](https://www.postgresql.org/docs/11/errcodes-appendix.html#ERRCODES-TABLE) in the PostgreSQL documentation. (The only syntax check is that the code is a mixture of five digits or upper-case Latin letters.) In contrast, when you raise an exception using its _name_, a syntax error occurs if the name is not found among the condition names that the table lists. In other words, the _raise \<exception name\>_ syntax lets you raise only exceptions that the PostgreSQL system itself can raise (and YSQL directly reuses the PostgreSQL implementation for this).

Nothing stops you doing this:

```plpgsql
do $body$
begin
  raise division_by_zero;
end;
$body$;
```

But doing so is at odds with the fact that the _raise_ syntax tautologically raises a user-defined exception. (It's user-defined exactly because the exception is raised as a consequence of logic that user-written code implements.) In other words, if you use, say, _raise division_by_zero_, then you're lying because your code, and not the PostgreSQL implementation, detected that you should do this—and therefore the error is not, from the strictly formal viewpoint, the one that _division_by_zero_ denotes.
{{< /tip >}}

#### "raise exception" with the "using" syntax

A very strong case can be made that a user-written PL/pgSQL subprogram that your privilege regime allows to be invoked from client-side application code should _never_ allow a SQL error to escape. Rather, every such subprogram should have a _when others_ hander that records all the available diagnostic information server-side, where client-side application code cannot access it, and do no more than end without an error and return a suitably encoded response to express "unexpected error" and the ticket number for the incident report so that Support can analyze what went wrong. This philosophy is extended by the notion of the regrettable, but not unexpected, error—exemplified by _"This nickname is taken. Please make a different choice."_ It is exactly this design paradigm that the _[hard-shell](https://github.com/YugabyteDB-Samples/ysql-case-studies/tree/main/hard-shell#hard-shell)_ case study, referred to above, implements.

(Briefly, the rationale is that hackers can learn a lot that helps them to exploit vulnerabilities from the slew of names of schemas, tables, columns, and the like that raw error messages expose.)

However, there's another use case where more than just the bare fact that a regrettable but not unexpected error occurred needs to be signalled as an exception. Suppose that an application accepts a name that an end-user enters using an interactive client-side application. The name must obey certain rules and so, in accordance with the usual approach, the interactive application checks that a newly-entered name does indeed obey the rules so that a bad name can be immediately rejected. Then the user can try again with the minimum of delay. This implies that only properly conformant names will be sent to the database.

Of course, there's a possibility that the client-side check is buggy so that a non-conformant name is sent. This must be seen as an unexpected error. Facts about it must be recorded in an _incidents_ table for offline analysis. But the database code doesn't need to be cluttered by code to communicate the outcome to the client application is it would for a regrettable, but not unexpected error.

The best way to deal with this scenario is at the deep level in the stack where a row with the name is to be inserted or updated. Then, if the conformance check for the name fails, a user-defined exception is raised with not just the dedicated error code for this but also further diagnostic information. The _message_ attribute might be enough. But it might be better to split the diagnostic information between a generic _message_ attribute and a specific _detail_ attribute. Then the API-level subprogram, at the top of the call-stack, that the client application calls will catch this user-defined exception in a _when others_ handler and look after recording the diagnostic information in the _incidents_ table and telling the client just that an unexpected error occurred, together with the incident's ticket number as. (This can be done, for example, using a JSON document as has been discussed above.)

The following code illustrates this approach. First, create the procedure _check_name()_ that finishes silently if the check succeeds and that raises an exception if it doesn't:

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;

create procedure s.check_name(proposed_name in text)
  security definer
  language plpgsql
as $body$
declare
  ok   boolean not null := true;
  err  constant text not null := 'YB257';
  msg  constant text    not null :=
    'Bad name: "'||coalesce(proposed_name, '<NULL>')||'".'                    ||e'\n'||
    'A name must have length() between 5 and 30, '                            ||e'\n'||              
    'must contain only lower case ASCII(7) letters, digits, or underscores, ' ||e'\n'||
    'and must not start with a digit or an underscore. ';
  detail text not null := '';
begin
  if proposed_name is null then
    ok     := false;
    detail := 'Name is null.';
  elsif not length(proposed_name) between 5 and 30 then
    ok     := false;
    detail := 'Name is too short or too long.';
  else
    declare
      -- Remove all characters except lower-case Latin letters, digits, and underscores.
      n1 constant text not null := regexp_replace(proposed_name, '[^a-z0-9_]', '', 'g');

      -- Remove any digit or underscore in just the first postion.
      n2 constant text not null := regexp_replace(n1, '^[0-9_]', '');
    begin
      case n1 = proposed_name
        when false then
           ok     := false;
           detail := 'Name contains illegal character(s).'; 
        else
          case n2 = n1
            when false then
              ok     := false;
              detail := 'Name starts with digit or underscore.'; 
            else
              ok :=  true;
          end case;
      end case;
    end;
  end if;

  if not ok then
    raise exception using
      errcode := err,
      message := msg,
      detail  := detail;
  end if;
end;
$body$;
```

Now create a table function that emulates to top-of-stack subprogram—just for the purpose of demonstrating that the diagnostic information is available at the level:

```plpgsql
create function s.check_name_outcome(proposed_name in text)
  returns table(z text)
  security definer
  language plpgsql
as $body$
begin
  call s.check_name(proposed_name);
  z := '"'||proposed_name||'" is OK.';                      return next;
exception
  when sqlstate 'YB257' then
    declare
      v_message_text         text not null := '';
      v_pg_exception_detail  text not null := '';
      v_pg_exception_context text not null := '';
    begin
      get stacked diagnostics
        v_message_text         := message_text,
        v_pg_exception_detail  := pg_exception_detail,
        v_pg_exception_context := pg_exception_context;

      z := v_message_text;                                  return next;
      z := '';                                              return next;
      z := v_pg_exception_detail;                           return next;
      z := '';                                              return next;
      z := v_pg_exception_context;                          return next;
    end;
end;
$body$;
```

Now test the scheme, first with a conformant name:

```plpgsql
select s.check_name_outcome('catch_22');
```

This is the result:

```output
 "catch_22" is OK.
```

Now test the scheme with a set of names, each of which violates one of the conformance rules so that all rules are tested. Use these names:

```output
null
'abcd'
'catch$22'
'42mouse'
'_elephant'
```

Here's the output from using the first of these non-conformant names:

```output
 Bad name: "<NULL>".                                                    +
 A name must have length() between 5 and 30,                            +
 must contain only lower case ASCII(7) letters, digits, or underscores, +
 and must not start with a digit or an underscore. 
 
 Name is null.
 
 PL/pgSQL function s.check_name(text) line 43 at RAISE                  +
 SQL statement "CALL s.check_name(proposed_name)"                       +
 PL/pgSQL function s.check_name_outcome(text) line 3 at CALL
```

All of the remaining negative tests show the same second and subsequent _message_ lines, and the same _context_ text as the first negative test—and so these lines are elided now. These are the remaining results:

```output
 Bad name: "abcd".                                                      +
 ... 
 
 Name is too short or too long.
```

and:

```output
 Bad name: "catch$22".                                                  +
 ...
 
 Name contains illegal character(s).
```

and:

```output
 Bad name: "42mouse".                                                   +
 ... 
 
 Name starts with digit or underscore.
```

and:

```output
 Bad name: "_elephant".                                                 +
 ... 
 
 Name starts with digit or underscore.
```

A similar scenario could arise if you're writing reusable subprograms for general use. (You might intend to package these up as an extension.) You can't control the use of such code—but you might, at least, expect that it will be called typically from application PL/pgSQL. Here, too, you have to deal with a regrettable but not unexpected error by raising a user-defined exception. You can see an example of this approach in the subsection [Custom domain types for specializing the native interval functionality](../../../../../../datatypes/type_datetime/date-time-data-types-semantics/type-interval/custom-interval-domains/) within the overall section [Date and time data types and functionality](../../../../../../datatypes/type_datetime/). These domain types have constraints which are implemented in user-defined PL/pgSQL code. There, contrary to the general advice given above, the reserved error code _23514_ (mapped to _check_violation_) is re-used.
