---
title: The PL/pgSQL "return" statement [YSQL]
headerTitle: The PL/pgSQL "return" statement
linkTitle: >
  "return" statement
description: Describes the syntax and semantics various PL/pgSQL "return" statement variants[YSQL].
menu:
  stable:
    identifier: return-statement
    parent: basic-statements
    weight: 40
type: docs
showRightNav: true
---

## Syntax

{{%ebnf%}}
  plpgsql_return_stmt
{{%/ebnf%}}

## Semantics

### The bare "return" statement

You can use the bare _return_ statement to exit from any top-level PL/pgSQL block statement (even if it is invoked within a deeply-nested block statement). In particular, you can use it to exit from a procedure or a _do_ statement. However, this use is very rarely seen—and many programers would consider it to be a bad practice. Rather, the preferred practice is to let the point of execution simply reach the final _end;_— by virtue of the use of "proper" flow control with the _if_ statement or the _case_ statement. Then the procedure is automatically exited just as if the bare _return_ were written here (and only here).

### The "return expression" statement

This is legal only in a regular function (but _not_ in a table function). In fact every regular function _must_ have at least one such statement and the point of execution must reach one of these. If you fail to satisfy this rule, then there's no syntax error. But you'll get the _2F005_ run-time error saying that the function finished without executing a _return expression_ statement. The expression must match what the _returns_ clause, in the function's header, specifies.

### The "return next" statement

This is legal only in a table function. In fact every table function _must_ have at least one such statement and the point of execution must reach one of these. Often, the _return next_ statement is written within a loop so that the table function returns many rows. Notice that the _return next_ statement takes no argument. Rather, you must assign value(s) ordinarily before invoking it, just as if these identified ordinary _in out_ formal arguments, using the identifier(s) that the _returns table(...)_ clause, in the function's header, specifies.

### The "return new", "return old" and "return null" statements

These are legal only in a DML trigger function. In fact every DML trigger function _must_ have at least one such statement and the point of execution must reach one of these. A DML trigger function is created as such by using the _returns trigger_ clause in the function's header.

<!--- _to_do_ --->
{{< note title="Coming soon" >}}
More detail, and code examples, about these uses will follow.
{{< /note >}}
