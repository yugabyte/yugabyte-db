---
title: Loop, exit, and continue statements [YSQL]
headerTitle: Loop, exit, and continue statements
linkTitle:
  The "loop", "exit", and "continue" statements
description: Describes the syntax and semantics of Loop, exit, and continue statements. [YSQL]
image: /images/section_icons/api/subsection.png
menu:
  preview:
    identifier: loop-exit-continue
    parent: compound-statements
    weight: 40
type: indexpage
showRightNav: true
---

## Syntax

### The "loop" statement

{{%ebnf%}}
  plpgsql_loop_stmt,
  plpgsql_unbounded_loop_defn,
  plpgsql_bounded_loop_defn,
  plpgsql_integer_for_loop_defn,
  plpgsql_array_foreach_loop_defn,
  plpgsql_query_for_loop_defn,
  plpgsql_dynamic_subquery
{{%/ebnf%}}

### The "exit" and "continue" statements

{{%ebnf%}}
  plpgsql_exit_stmt,
  plpgsql_continue_stmt
{{%/ebnf%}}

## Semantics

There are two kinds of PL/pgSQL loop: the _unbounded loop_; and the (bounded) _for loop_.

- The number of iterations that an _unbounded loop_ performs isn't given by a simple recipe. Rather, iteration continues until it is interrupted, at any statement within its statement list, by invoking _exit_.
- In contrast, the (maximum) number of iterations that a _for loop_ performs is determined, before the first iteration starts. The recipe might, for example, be just the consecutive integers between a lower bound and an upper bound. Or it might be "for every element in the specified array", or "for every row in the result set of a specified query.

The functionality of all kinds of loops is complemented by the _exit_ statement and the _continue_ statement. The _exit_ statement aborts the iteration altogether. And the _continue_ statement aborts just the current iteration and then starts the next one.

See the section [Two case studies: Using various kinds of "loop" statement, the "exit" statement, and the "continue" statement](../loop-exit-continue/two-case-studies/) for realistic uses of all of the statements (except for the _while loop_) that this page describes.

### Unbounded loop

The name _unbounded_ denotes the fact that the number of iterations that the loop will complete is not announced at the start. Rather, iteration ends when facts that emerge while it executes determine that it's time to stop iterating. There are two kinds of _unbounded loop_: the _infinite loop_; and the _while loop_. See the dedicated page [The "infinite loop" and the "while loop"](./infinite-and-while-loops/).

### "Exit" statement

Most usually, the _exit_ statement is written within a _loop_ statement—but see the note immediately below.

The _exit_ statement aborts the execution of the loop. Notice the optional _label_. It must match an _end loop_ statement (or the _end_ statement of a block statement) with the same _label_ within the current top-level block statement.

- When the _label_ is omitted, the point of execution moves to the statement that immediately follows the present loop's _end loop_ statement.
- When _exit some_label_ is used, the point of execution moves to the statement that immediately follows the _end loop_ statement (or the bare _end_ statement of the block statement) that has the same label.

{{< note title="An 'exit' statement's 'label' must match that of an 'end loop' statement or that of the 'end' statement of a block statement." >}}
See the dedicated section [Using the "exit" statement to jump out of a block statement](./exit-from-block-statememt/).
{{< /note >}}

### "Continue" statement

The _continue_ statement is legal only within a _loop_ statement.

- When _label_ is omitted, the _continue_ statement causes the current iteration to be abandoned and the next one to start immediately.

- When _label_ is specified, it causes the current iteration of the most tightly enclosing loop, and any loops in which it is nested through the one whose _end loop_ matches the _label_, to be abandoned. Then the next iteration of the loop whose _end loop_ matches the _label_ starts immediately.

-  If used, the _label_ must match that of an _end loop_ statement.

It's possible to write the _exit_ or the _continue_ statement in one of the legs of an _if_ statement or a _case_ statement in the executable section, or even in the exception section, of a block statement at any nesting depth. Probably, in such a context, you'd omit the optional _when_ clause.

### Bounded loop

The name _bounded_ denotes the fact that the (maximum) number of iterations that the loop will complete is computed, just before the first iteration, on entry into the loop. The qualifier "maximum" is used because the _exit_ statement can be used to cause premature exit.

There are three kinds of _bounded loop_ loop:

- the _[integer for loop](./integer-for-loop/)_ — defined by the _plpgsql_integer_for_loop_defn_ syntax rule
- the _[array foreach loop](./array-foreach-loop/)_ — defined by the _plpgsql_array_foreach_loop_defn_ syntax rule
- The  _[query for loop](./query-for-loop/)_ — defined by the _plpgsql_query_for_loop_defn_ syntax rule.
