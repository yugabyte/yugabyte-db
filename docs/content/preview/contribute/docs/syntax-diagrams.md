---
title: Edit syntax diagrams
headerTitle: Edit syntax diagrams
linkTitle: Syntax diagrams
description: Edit syntax diagrams
headcontent: How to edit syntax diagrams
menu:
  preview:
    identifier: docs-syntax-diagrams
    parent: docs-edit
    weight: 3000
rightNav:
  hideH4: true
type: docs
---

This document describes how to make changes to YSQL API [_syntax diagrams_](#syntax-diagram) or add new ones.

{{< note >}}
The following covers diagram maintenance and creation for the YSQL documentation. The YCQL documentation still uses an old method for this. You must take advice from colleagues if you need to work on YCQL diagrams.
{{< /note >}}

## Terminology

Let's go over a few terminology before understanding how to modify syntax diagrams.

### Syntax rule

A [_syntax rule_](#syntax-rule) is the formal definition of the grammar of a SQL statement, or a component of a SQL statement.

Every [_syntax rule_](#syntax-rule) is defined textually in the single [_diagram definition file_](#diagram-definition-file). The set of all these rules is intended to define the entirety of the YSQL grammar—but nothing beyond this. Presently, the definitions of some [_syntax rules_](#syntax-rule) (while these are implemented in the YSQL subsystem of YugabyteDB) remain to be written down.

Sometimes, the grammar of an entire SQL statement can be comfortably described by a single, self-contained [_syntax rule_](#syntax-rule). The [Syntax section](/preview/api/ysql/the-sql-language/statements/txn_commit/#syntax) of the account of the `COMMIT` statement provides an example. More commonly, the grammar of a SQL statement includes references (by name) to the definition(s) of one or more other rule(s). And often such referenced [_syntax rules_](#syntax-rule) are the targets of references from many other [_syntax rules_](#syntax-rule). The complete account of a very flexible SQL statement can end up as a very large closure of multiple referenced rules. [`SELECT`](/preview/api/ysql/the-sql-language/statements/dml_select/#syntax) is the canonical example of complexity. For example, a terminal like [`integer`](/preview/api/ysql/syntax_resources/grammar_diagrams/#integer) can end up as the reference target in very many distinct syntax spots within the total definition of the `SELECT` statement, and of other statements.

A [_syntax rule_](#syntax-rule) is specified using EBNF notation. EBNF stands for "extended Backus–Naur form". See this [Wikipedia article](https://en.wikipedia.org/wiki/Extended_Backus–Naur_form). Here is an example of the [`PREPARE`](/preview/api/ysql/the-sql-language/statements/perf_prepare/#syntax) statement:

```ebnf
prepare_statement ::= 'PREPARE' name [ '(' data_type { ',' data_type } ')' ] 'AS' statement ;
```

> **Note:** When this is presented on the "Grammar" tab in the [_syntax diagram_](#syntax-diagram), it is transformed into the PostgreSQL notation. This is widely used in database documentation. But it is not suitable as input for a program that generate diagrams.

Notice the following:

- The LHS of `::=` is the rule's name. The RHS is its definition. The definition has three kinds of element:

- _EBNF syntax elements_ like these: `[  ]  (  )  '  {  }  ,`

- _Keywords and punctuation that are part of the grammar that is being defined_. Keywords are conventionally spelled in upper case. Each individual keyword is surrounded by single quotes. This conveys the meaning that whitespace between these, and other elements in the target grammar, is insignificant. Punctuation characters in the target grammar are surrounded by single quotes to distinguish them from punctuation characters in the EBNF grammar.

- _References to other rule names_. These become clickable links in the generated diagrams (but not in the generated grammars). Rule names are spelled in lower case. Notice how underscore is used between the individual English words.

- The single space before the `;` terminator is significant.

### Syntax diagram

- **The result, in the published documentation, of a [_syntax rule_](#syntax-rule) that is contained in the [_diagram definition file_](#diagram-definition-file)**.

The [_syntax diagram_](#syntax-diagram) and the [_syntax rule_](#syntax-rule) bear a one-to-one mutual relationship.

The [Syntax section](/preview/api/ysql/the-sql-language/statements/perf_prepare/#syntax) of the account of the [`PREPARE`](/preview/api/ysql/the-sql-language/statements/perf_prepare/) statement provides a short, but sufficient, example. The [_syntax diagram_](#syntax-diagram) appears as a (member of a) tabbed pair which gives the reader the choice to see a [_syntax rule_](#syntax-rule) as either the "Grammar" form (in the syntax used in the PostgreSQL documentation and the documentation for several other databases) or the "Diagram" form (a so-called "railroad diagram" that again is used commonly in the documentation for several other databases).

### Syntax diagram set

A set of `ebnf` rule names grouped and displayed together on a content page.

### Diagram definition file

The `ysql_grammar.ebnf` located at `/docs/content/preview/api/ysql/syntax_resources/` holds the definition, written in EBNF notation, of every [_syntax rule_](#syntax-rule). This file in all its entirety is manually typed.

The order in which the rules are specified in this file is reproduced in the [_grammar diagrams file_](#grammar-diagrams-file). This, in turn, reflects decisions made by authors about what makes a convenient reading order. Try to spot what informs the present ordering and insert new rules in a way that respects this. In the limit, insert a new rule at the end as the last new properly-defined rule and before this comment:

```ebnf
(* Supporting rules *)
```

### Grammar diagrams file

The grammar [diagrams file](/preview/api/ysql/syntax_resources/grammar_diagrams/#abort) contains every [_syntax diagram_](#syntax-diagram) that is generated from all of the [_syntax rules_](#syntax-rule) that are found in the [_diagram definition file_](#diagram-definition-file).


## Add or Modify rules

The grammar and diagrams are generated dynamically on the docs site. The diagram/grammar pair are automatically populated based on the syntax rules. There are typically 2 workflows here.

1. Add or modify grammar definitions
1. Add or modify the syntax rules to be displayed on a page

### Add/Modify grammar definition

To add a new grammar definition or modify an existing grammar, edit the [_diagram definition file_](#diagram-definition-file).

{{<note title="Note">}}
If you are developing on a local machine, you need to restart Hugo after modifying the [_diagram definition file_](#diagram-definition-file) for the changes to be reflected on the local preview.
{{</note>}}

### Add/Modify syntax rules on a page

To add a syntax rule on a page, use the `ebnf` shortcode and specify each rule in a line terminated with a comma in the content of the shortcode. For example, for [window function invocations](/preview/api/ysql/exprs/window_functions/invocation-syntax-semantics#syntax), to show `select_start` and `window_clause` on a page, you need to do the following.

```ebnf
{{%/*ebnf*/%}}
  select_start,
  window_clause
{{%/*ebnf*/%}}
```

This would add the grammar and syntax tabs like this:

{{%ebnf%}}
  select_start,
  window_clause
{{%/ebnf%}}

The syntax and grammar diagrams are generated in the same order as included in the `ebnf` shortcode. Suppose that a [_syntax rule_](#syntax-rule) includes a reference to another [_syntax rule_](#syntax-rule). If the referenced [_syntax rule_](#syntax-rule) is included in the same [_syntax diagram set_](#syntax-diagram-set), then the name of the [_syntax rule_](#syntax-rule) in the referring [_syntax diagram_](#syntax-diagram) becomes a link to the [_syntax rule_](#syntax-rule) in that same [_syntax diagram set_](#syntax-diagram-set). Otherwise the generated link target of the referring rule is within the [_grammar diagrams file_](#grammar-diagrams-file). The way that this link is spelled depends on the location, within the [_ysql directory_](#ysql-directory) tree, of the `.md` file that includes the generated [_syntax diagram_](#syntax-diagram).

In the case you have multiple [_syntax diagram sets_](#syntax-diagram-set) on the same page and would like to cross-reference each other on the same page, specify the local rules that need to be cross referenced as comma separated values in the `localrefs` argument of the `ebnf` shortcode. For example,

```ebnf
{{%/*ebnf localrefs="window_definition,frame_clause"*/%}}
```

This will ensure that any reference to `window_definition` or `frame_clause` in this [_syntax diagram set_](#syntax-diagram-set) will link to another [_syntax diagram set_](#syntax-diagram-set) on the same page and not to the [_grammar diagrams file_](#grammar-diagrams-file).


## Caveats

### Redundant use of ( ... )

In at least one case, the use of semantically redundant notation in the [_diagram definition file_](#diagram-definition-file) conveys the author's intent about how the diagram should be constructed. Here's an example of semantically significant use of `( ... )` in algebra:

&nbsp;&nbsp;`(a/b) - c` differs in meaning from `a/(b - c)`

And while the rules do give `a/b - c` an unambiguous meaning (advertised by the absence of space surrounding the `/` operator), it's generally considered unhelpful to write this, thereby forcing the reader to rehearse the rules in order to discern the meaning. Rather, the semantically redundant orthography `(a/b) - c` is preferred.

In EBNF, `( ... )` plays a similar role as it does in algebra. In some situations, just which subset of items is grouped changes the meaning. But in some situations (like `a + b + c` in algebra) the use of `( ... )` conveys no semantics. Look for this at the end of the [_diagram definition file_](#diagram-definition-file):

```ebnf
(*
  Notice that the "demo-2" rule uses ( ... ) redundantly.
  The two rules are semantically identical but they produce the different diagrams.
  The diagrams do express the same meaning but they are drawn using different conventions.
  Uncomment the two rule definitions and look at the end of the grammar diagrams file to
  see the two semantically equivalent, but differently drawn, diagrams.
  Make sure that you comment these out again before creating a Pull request.
*)

(*
 demo-1-irrelevant-for-ysql-syntax ::= ( a 'K' b { ',' a 'K' b } ) ;
 demo-2-irrelevant-for-ysql-syntax ::= ( ( a 'K' b ) { ',' ( a 'K' b ) } ) ;
*)
```

### Missing the single-quotes around a keyword

Look for this syntax rule in the [_diagram definition file_](#diagram-definition-file):

```ebnf
declare = 'DECLARE' cursor_name [ 'BINARY' ] [ 'INSENSITIVE' ] [ [ 'NO' ] 'SCROLL' ] \
          'CURSOR' [ ( 'WITH' | 'WITHOUT' ) 'HOLD' ] 'FOR' subquery ;
```

Remove the single quotes that surround `'INSENSITIVE'`. This will change its status in EBNF's grammar from _keyword_ to _syntax rule_. Most people find it hard to spot such a typo just by proofreading. Now re-run the diagram generator. You won't see any errors reported on _stderr_. But if you look carefully at the _stdout_ report, you'll see this warning:

```bash
WARNING: Undefined rules referenced in rule 'declare': [INSENSITIVE]
```

It's clear what it means. And you must fix it because otherwise a reader of the YSQL documentation will see a semantic error—and will wonder what on earth the _insensitive_ rule is. Fix the problem immediately and restart Hugo. Then see a clean report again. This exercise tells you that it's a very good plan to restart Hugo after every edit to any single grammar rule. You'll know what rule you just edited and so you'll immediately know where to look for the error.

### Misspelling a syntax rule

Look for this in the [_diagram definition file_](#diagram-definition-file):

```ebnf
savepoint_rollback = ( 'ROLLBACK' ['WORK' | 'TRANSACTION' ] 'TO' [ 'SAVEPOINT' ] name ) ;
```

Edit it to change, say, the first `]` character to `}`. It's easy to do this typo because these two characters are on the same key and it's hard to see the difference when you use a small font. Now restart Hugo. You'll see this on _stderr_:

```java
Exception in thread "main" java.lang.IllegalStateException: This element must not be nested and should have been processed before entering generation.
	at net.nextencia.rrdiagram.grammar.rrdiagram.RRBreak.computeLayoutInfo(RRBreak.java:19)
	at net.nextencia.rrdiagram.grammar.rrdiagram.RRSequence.computeLayoutInfo(RRSequence.java:34)
	at net.nextencia.rrdiagram.grammar.rrdiagram.RRChoice.computeLayoutInfo(RRChoice.java:30)
	at net.nextencia.rrdiagram.grammar.rrdiagram.RRSequence.computeLayoutInfo(RRSequence.java:34)
	at net.nextencia.rrdiagram.grammar.rrdiagram.RRDiagram.toSVG(RRDiagram.java:333)
	at net.nextencia.rrdiagram.grammar.rrdiagram.RRDiagramToSVG.convert(RRDiagramToSVG.java:30)
	at net.nextencia.rrdiagram.Main.regenerateReferenceFile(Main.java:139)
	at net.nextencia.rrdiagram.Main.regenerateFolder(Main.java:72)
	at net.nextencia.rrdiagram.Main.main(Main.java:54)
```

The error will cause the notorious _Hugo black screen_ in the browser. It's best to _\<ctrl\>-C_ Hugo now.

This is hardly user-friendly! You'll also see several warnings on _stdout_. Almost all of these are simple consequences of the actual problem and so tell you nothing. Here's the significant information:

```sql
WARNING: Exception occurred while exporting rule savepoint_rollback
WARNING: savepoint_rollback = 'ROLLBACK' [ 'WORK' | 'TRANSACTION' } 'TO' [ 'SAVEPOINT' ] name ) ; ...
```

You can see that the offending `}` is mentioned. Fix it immediately, restart Hugo, and then see a clean report again. This exercise, too, tells you that you should restart Hugo after every edit to a grammar rule. Here too, you'll know what rule you just edited and so will immediately know where to look for the error.
