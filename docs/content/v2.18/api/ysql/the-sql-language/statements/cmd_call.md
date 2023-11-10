---
title: CALL statement [YSQL]
headerTitle: CALL
linkTitle: CALL
description: Use the CALL statement to execute a stored procedure.
menu:
  v2.18:
    identifier: cmd_call
    parent: statements
type: docs
---

## Synopsis

Use the `CALL` statement to execute a stored procedure.

## Syntax

{{%ebnf%}}
  call_procedure,
  actual_arg,
  formal_arg
{{%/ebnf%}}

{{< tip title="The syntax and semantics of 'subprogram_arg' are the same for function invocation as for 'CALL'." >}}
The syntax and semantics of the _subprogram_arg_ rule (for example how to use the named parameter invocation style to avoid providing actual arguments for defaulted parameters) are the same for invoking a function as for `CALL`. A function cannot be invoked with the `CALL` statement. Rather, it's invoked as (part of) an expression in DML statements like `SELECT` or in PL/pgSQL source code.
{{< /tip >}}

## Semantics

`CALL` executes a stored procedure. If the procedure has any output parameters, then a result row will be returned, containing the values of those parameters.

The caller must have _both_ the _usage_ privilege on the schema in which the to-be-called procedure exists _and_ the  _execute_ privilege on it. If the caller lacks the required _usage_ privilege, then it causes this error:

```output
42501: permission denied for schema %"
```

If the caller has the required _usage_ privilege but lacks the required _execute_ privilege, then it causes this error:

```output
42501: permission denied for procedure %
```

## Notes

If `CALL` is executed in a transaction block, then it cannot execute transaction control statements. The attempt causes this runtime error:

```output
2D000: invalid transaction termination
```

Transaction control statements are  allowed when `CALL` is invoked with _autocommit_ set to _on_—in which case the procedure executes in its own transaction.

## Simple example

Create a simple procedure

```plpgsql
set client_min_messages = warning;
drop procedure if exists p(text, int) cascade;
create procedure p(
  caption in text default 'Caption',
  int_val in int default 17)
  language plpgsql
as $body$
begin
  raise info 'Result: %: %', caption, int_val::text;
end;
$body$;
```

Invoke it using the simple syntax:

```plpgsql
call p('Forty-two', 42);
```
This is the result:

```output
INFO:  Result: Forty-two: 42
```
Omit the second defaulted parameter:

```plpgsql
call p('"int_val" default is');
```

This is the result:

```output
INFO:  Result: "int_val" default is: 17
```

Omit the both defaulted parameters:

```plpgsql
call p();
```

This is the result:

```output
INFO:  Result: Caption: 17
```

Invoke it by using the names of the formal parameters.

```plpgsql
call p(caption => 'Forty-two', int_val=>42);
```

This is the result:

```output
INFO:  Result: Forty-two: 42
```

Invoke it by just the named second parameter.

```plpgsql
call p(int_val=>99);
```

This is the result:

```output
INFO:  Result: Caption: 99
```

Provoke an error by using just the second unnamed parameter.

```plpgsql
call p(99);
```

It causes this error:

```output
42883: procedure p(integer) does not exist
```
In this case, this generic hint:

```output
You might need to add explicit type casts.
```

isn't helpful.

## Example with 'inout' arguments

Create a procedure with `INOUT` arguments.

```plpgsql
drop procedure if exists x(int, int, int, int, int);
create procedure x(
  a inout int,
  b inout int,
  c inout int,
  d inout int,
  e inout int)
  language plpgsql
as $body$
begin
  a := a + 1;
  b := b + 2;
  c := c + 3;
  d := d + 4;
  e := e + 5;
end;
$body$;
```

Notice that this is rather strange. The ordinary meaning of an `INOUT` parameter is that its value will be changed by the invocation so that the caller sees different values after the call. Normally, a procedure designed and written to be called from another procedure or a `DO` block with actual arguments that are declared as variables. But when such a procedure is called using a top-level `CALL` statement, it returns the results in the same way that a `SELECT` statement does. Try this:

```plpgsql
call x(10, 20, 30, 40, 50);
```
This is the result:

```output
 a  | b  | c  | d  | e
----+----+----+----+----
 11 | 22 | 33 | 44 | 55
```

Here's how to invoke procedure _x()_ in PLpgSQL:

```plpgsql
do $body$
declare
  a_var int not null := 10;
  b_var int not null := 20;
  c_var int not null := 30;
  d_var int not null := 40;
  e_var int not null := 50;
begin
  call x(a_var, b_var, c_var, d_var, e_var);
  raise info 'Result: %, %, %, %, %', a_var, b_var, c_var, d_var, e_var;
end;
$body$;
```

This is the result:

```output
INFO:  Result: 11, 22, 33, 44, 55
```

You cannot create a procedure with `OUT` formal arguments. The attempt causes the error

```output
0A000: procedures cannot have OUT arguments
```

This is tracked by [Github Issue #12348](https://github.com/yugabyte/yugabyte-db/issues/12348).
