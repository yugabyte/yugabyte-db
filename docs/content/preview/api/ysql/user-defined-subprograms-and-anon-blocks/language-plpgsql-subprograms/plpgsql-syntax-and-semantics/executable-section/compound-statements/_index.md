---
title: Compound PL/pgSQL executable statements [YSQL]
headerTitle: Compound PL/pgSQL executable statements
linkTitle: Compound statements
description: Describes the syntax and semantics of the compound PL/pgSQL executable statements. [YSQL]
image: /images/section_icons/api/subsection.png
menu:
  preview:
    identifier: compound-statements
    parent: executable-section
    weight: 20
type: indexpage
showRightNav: true
---

The following table lists all of the [compound PL/pgSQL executable statements](../../../../../syntax_resources/grammar_diagrams/#plpgsql-compound-stmt).
- The _Statement Name_ column links to the page where the semantics are described.
- The _Syntax rule name_ column links to the definition on the omnibus [Grammar Diagrams](../../../../../syntax_resources/grammar_diagrams/) reference page.

| STATEMENT NAME                                | SYNTAX RULE NAME                                                                           | COMMENT                                                                                                                             |
| --------------------------------------------- | ------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------- |
| [block statement](../../#plpgsql-block-stmt)  | [plpgsql_block_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-block-stmt) | DECLARE \<declarations\> BEGIN \<executable statements\> EXCEPTION \<handlers\> END;                                                |
| [loop statement](./loop-exit-continue/)       | [plpgsql_loop_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-loop-stmt)   | [unbounded loop](./loop-exit-continue/infinite-and-while-loops/), [bounded loop](./loop-exit-continue/#bounded-loop)                |
| [if statement](./if-statement/)               | [plpgsql_if_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-if-stmt)       | IF \<test\> THEN \<executable statements\> ELSIF \<test\> THEN \<executable statements\> ... ELSE \<executable statements\> END IF; |
| [case statement](./case-statement/)           | [plpgsql_case_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-case-stmt)   | simple form: CASE \<expression\> WHEN \<value\> THEN <executable statements\> ... ELSE \<executable statements\> END CASE;          |
