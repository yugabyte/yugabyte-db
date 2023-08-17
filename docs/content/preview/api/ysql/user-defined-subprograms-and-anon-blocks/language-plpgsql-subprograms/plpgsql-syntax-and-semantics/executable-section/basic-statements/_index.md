---
title: Basic PL/pgSQL executable statements [YSQL]
headerTitle: Basic PL/pgSQL executable statements
linkTitle: Basic statements
description: Describes the syntax and semantics of the basic PL/pgSQL executable statements. [YSQL]
image: /images/section_icons/api/subsection.png
menu:
  preview:
    identifier: basic-statements
    parent: executable-section
    weight: 10
type: indexpage
showRightNav: true
---

The following table lists all of the [basic PL/pgSQL executable statements](../../../../../syntax_resources/grammar_diagrams/#plpgsql-basic-stmt).
- The _Statement Name_ column links to the page where the semantics are described.
- The _Syntax rule name_ column links to the definition on the omnibus [Grammar Diagrams](../../../../../syntax_resources/grammar_diagrams/) reference page.

| STATEMENT NAME                                                                                                 | SYNTAX RULE NAME                                                                                                               | COMMENT                                                                                                 |
| -------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------- |
| [assert](./assert/)                                                                                            | [plpgsql_assert_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-assert-stmt)                                   |                                                                                                         |
| assign                                                                                                         | [plpgsql_assignment_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-assignment-stmt)                           | e.g. "a := b + c;" and "v := (select count(*) from s.t)". No further explanation is needed              |
| [close](./cursor-manipulation/#plpgsql-close-cursor-stmt)                                                      | [plpgsql_close_cursor_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-close-cursor-stmt)                       |                                                                                                         |
| [continue](../compound-statements/loop-exit-continue/#continue-statement)                                      | [plpgsql_continue_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-continue-stmt)                               |                                                                                                         |
| execute                                                                                                        | [plpgsql_dynamic_sql_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-dynamic-sql-stmt)                         |                                                                                                         |
| [exit](../compound-statements/loop-exit-continue/#exit-statement)                                              | [plpgsql_exit_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-exit-stmt)                                       |                                                                                                         |
| [fetch](../compound-statements/loop-exit-continue/infinite-and-while-loops/#infinite-loop-over-cursor-results) | [plpgsql_fetch_from_cursor_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-fetch-from-cursor-stmt)             | fetch from cursor                                                                                       |
| [get diagnostics](./get-diagnostics/)                                                                          | [plpgsql_get_diagnostics_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-get-diagnostics-stmt)                 |                                                                                                         |
| [get stacked diagnostics](../../exception-section/#how-to-get-information-about-the-error)                     | [plpgsql_get_stacked_diagnostics_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-get-stacked-diagnostics-stmt) |                                                                                                         |
| move                                                                                                           | [plpgsql_move_in_cursor_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-move-in-cursor-stmt)                   | move in cursor — not yet supported, see [Beware Issue #6514](../../../../../cursors/#beware-issue-6514) |
| [open](./cursor-manipulation/#plpgsql-open-cursor-stmt)                                                        | [plpgsql_open_cursor_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-open-cursor-stmt)                         | open cursor                                                                                             |
| perform                                                                                                        | [plpgsql_perform_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-perform-stmt)                                 | execute a select statement without returning rows                                                       |
| [raise](./raise/)                                                                                              | [plpgsql_raise_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-raise-stmt)                                     | raise info,... warning,... exception                                                                    |
| return                                                                                                         | [plpgsql_return_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-return-stmt)                                   | exit from subprogram to caller, optionally returning value(s)                                           |
| SQL                                                                                                            | [plpgsql_static_bare_sql_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-static-bare-sql-stmt)                 | embedded SQL statement with no returned values                                                          |
| SQL DML                                                                                                        | [plpgsql_static_dml_returning_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-static-dml-returning-stmt)       | embedded SQL statement with "into" clause for "returning"                                               |
| select into                                                                                                    | [plpgsql_static_select_into_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-static-select-into-stmt)           | embedded single-row "select" with "returning"                                                           |

