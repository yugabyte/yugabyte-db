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

| STATEMENT NAME                                | SYNTAX RULE NAME                                                                           | COMMENT |
| --------------------------------------------- | ------------------------------------------------------------------------------------------ | ------- |
| [block statement](../../#plpgsql-block-stmt)  | [plpgsql_block_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-block-stmt) | **declare** _\<declarations\>_<br /> **begin** _\<executable statements\>_<br /> **exception** _\<handlers\>_<br /> **end;** |
| [case statement](./case-statement/)           | [plpgsql_case_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-case-stmt)   | _<ins>Simple form</ins>:_<br /> **case** _\<expression\>_<br /> **when** _\<value\>_ **then** _\<executable statements\>_ ...<br /> **else** _\<executable statements\>_<br /> **end case;** <br /><br /> _<ins>Searched form</ins>:_<br /> **case** <br /> **when** _\<boolean expression\>_ **then** _\<executable statements\>_ ...<br /> **else** _\<executable statements\>_<br /> **end case;** |
| [if statement](./if-statement/)               | [plpgsql_if_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-if-stmt)       | **if** _\<boolean expression\>_ **then** _\<executable statements\>_<br /> **elsif** _\<boolean expression\>_ **then** _\<executable statements\>_ ...<br /> **else** _\<executable statements\>_<br /> **end if;** |
| [loop statement](./loop-exit-continue/)       | [plpgsql_loop_stmt](../../../../../syntax_resources/grammar_diagrams/#plpgsql-loop-stmt)   | [unbounded loop](./loop-exit-continue/infinite-and-while-loops/) _or_ [bounded loop](./loop-exit-continue/#bounded-loop) |
