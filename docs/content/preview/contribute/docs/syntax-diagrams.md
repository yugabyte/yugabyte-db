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
type: docs
---

In this kind of work, the nature of your documentation change means that you'll need to modify existing [_syntax diagrams_](#syntax-diagram) or add new ones.

{{< note >}}
The following covers diagram maintenance and creation for the YSQL documentation. The YCQL documentation still uses an old method for this. You must take advice from colleagues if you need to work on YCQL diagrams.
{{< /note >}}

After you understand this area, modifying existing [_syntax diagrams_](#syntax-diagram) or adding new ones will seem to be very straightforward. However, you must embrace a fairly large mental model. This, in turn, depends on several terms of art. All this is covered in the dedicated section [Creating and maintaining syntax diagrams for YSQL Docs](#mental-model-and-glossary-creating-and-maintaining-syntax-diagrams-for-ysql-docs).

Here is a terse step-by-step summary:

## Get set up

1. Download the latest RRDiagram JAR file (`rrdiagram.jar`). See the section the [_diagram generator_](#diagram-generator).

1. [_Run the generator_](#run-the-generator). This regenerates every diagram file that is jointly specified by the [_diagram definition file_](#diagram-definition-file) and the set of all [_free-standing generated grammar-diagram pairs_](#free-standing-generated-grammar-diagram-pair). Of course, not all of these newly generated `md` files will differ from their previous versions; `git status` will show you only the ones that actually changed.

## Modify or add content pages or syntax rules

The nature of your documentation change will determine which selection of the following tasks you will need to do and which of these you will do during each local edit-and-review cycle.

- Modify [_humanly typed documentation source_](#humanly-typed-documentation-source) file(s).
- Create new [_humanly typed documentation source_](#humanly-typed-documentation-source) file(s).
- Modify existing [_syntax rule(s)_](#syntax-rule).
- Define new [_syntax rule(s)_](#syntax-rule).
- Create new [_free-standing generated grammar-diagram pair(s)_](#free-standing-generated-grammar-diagram-pair)
- Add [_diagram inclusion HTML_](#diagram-inclusion-html) to one or more of the content files that you modified or created.

Very occasionally, you might want to reorganize the hierarchical structure of a part of the overall documentation as the user sees it. (This is the hierarchy that you see and navigate in the left-hand navigation panel.) Such changes involve moving existing  [_humanly typed documentation source_](#humanly-typed-documentation-source) file(s) in the directory tree that starts at the [_content directory_](#content-directory). If you do this, then you must use your favorite editor (a generic plain text editor is best for this purpose) to do manually driven global search and replace to update URL references to moved files at their old locations. However, because you will do this only in the scope of the files that you worked on (most likely, the `/preview/` subtree), you must also establish URL redirects in each moved file to avoid breaking links in other Yugabyte Internet properties, or in third party sites. The _frontmatter_ allows this easily. Here is an example.

```yaml
title: SELECT statement [YSQL]
headerTitle: SELECT
linkTitle: SELECT
description: Use the SELECT statement to retrieve rows of specified columns that meet a given condition from a table.
menu:
  preview:
    identifier: dml_select
    parent: statements
aliases:
  - /preview/api/ysql/commands/dml_select/
```

The `aliases` page property allows a list of many URLs. Notice that these are _relative_ to the [_content directory_](#content-directory). The `.md` file in this example used to be here:

```output
<your path>/yugabyte-db/docs/content/preview/api/ysql/commands/dml_select.md
```

It was moved to here:

```output
<your path>/yugabyte-db/docs/content/preview/api/ysql/the-sql-language/statements/dml_select.md
```

Your specific documentation enhancement will determine if you need only to change existing content pages or to add new ones. You will decide, in turn, if you need to modify any existing [_syntax rules_](#syntax-rule) or to define new ones. And if you do define new ones, you will decide in which files you need to include the "grammar" and "diagram" depictions. (See the section about [_free-standing generated grammar-diagram pairs_](#free-standing-generated-grammar-diagram-pair).)

Follow this general flow on each local editing cycle.

1. Modify content pages, or if necessary create new ones.
1. If necessary, edit the [_diagram definition file_](#diagram-definition-file).
1. If necessary, create one or more new [_free-standing generated grammar-diagram pairs_](#free-standing-generated-grammar-diagram-pair), using `touch`, on the appropriate directories in the [_syntax resources directory_](#syntax-resources-directory) tree.
1. If necessary, [_run the generator_](#run-the-generator).
1. If necessary, add [_diagram inclusion HTML_](#diagram-inclusion-html) to one or more of the content files that you modified.
1. If necessary, add URL references in other content files to link to your new work. For example, you might need to update an `_index.md` file that implements a table of contents to a set of files to which you've just added one.
1. If necessary, add (or update) the `aliases` page property in any  [_humanly typed documentation source_](#humanly-typed-documentation-source) file(s) that you relocated.
1. Manually check the new docs page(s) and index page(s) to make sure that there are no broken links.

> **Note:** There is no tool for link checking within your local git. Broken links are detected when a Yugabyte staff member periodically runs a link checker on the published doc on the Internet. The scope of this check includes _all_ of the Yugabyte properties (for example, the [Blogs site](https://blog.yugabyte.com)). Broken links that this check finds are reported and fixed manually.

## Mental model and glossary: creating and maintaining syntax diagrams for YSQL Docs

This section describes the mental model for the creation and maintenance of [_syntax diagrams_](#syntax-diagram). The description relies on a number of terms of art. These definitions must be in place in order to explain, efficiently and unambiguously, the mental model of how it all works. Each glossary term is italicized, and set up as a link to the term's definition in this essay, whenever it is used to advertise its status as a defined term of art.

The ordering of the glossary terms is insignificant. There's a fair amount of term-on-term dependency. You therefore have to internalize the whole account by ordinary study. When the mental model that they reflect is firmly in place, you can produce any outcome that you want in this space without step-by-step instructions. In particular, you can base the diagnosis of author-created bugs (for example, broken links) on the mental model.

> **Note:** The account that follows distinguishes between _directory_ and _file_ in the usual way: a _directory_ contains _files_ and/or _directories_. And a _file_ has actual content.
>
> **Note:** the two terms, "grammar" and "syntax", mean pretty much the same as each other. But one or the other of these is used by convention in the spellings and definitions of the terms of art that this glossary defines.
>
> **Note:** Users of the Internet-facing YugabyteDB documentation typically access the sub-corpus that starts here:
>
> [`docs.yugabyte.com/preview/`](https://docs.yugabyte.com/preview/)
>
> Users with more specific requirements will start at `.../stable/` or, maybe, something like `.../v2.1/`. This account assumes that users will work on content only in the `/preview/` subtree.

### Syntax rule

- **The formal definition of the grammar of a SQL statement, or a component of a SQL statement**.

Every [_syntax rule_](#syntax-rule) is defined textually in the single [_diagram definition file_](#diagram-definition-file). The set of all these rules is intended to define the entirety of the YSQL grammar—but nothing beyond this. Presently, the definitions of some [_syntax rules_](#syntax-rule) (while these are implemented in the YSQL subsystem of YugabyteDB) remain to be written down.

Sometimes, the grammar of an entire SQL statement can be comfortably described by a single, self-contained [_syntax rule_](#syntax-rule). The [Syntax section](/preview/api/ysql/the-sql-language/statements/txn_commit/#syntax) of the account of the `COMMIT` statement provides an example. More commonly, the grammar of a SQL statement includes references (by name) to the definition(s) of one or more other rule(s). And often such referenced [_syntax rules_](#syntax-rule) are the targets of references from many other [_syntax rules_](#syntax-rule). The complete account of a very flexible SQL statement can end up as a very large closure of multiply referenced rules. [`SELECT`](/preview/api/ysql/the-sql-language/statements/dml_select/#syntax) is the canonical example of complexity. For example, a terminal like [`integer`](/preview/api/ysql/syntax_resources/grammar_diagrams/#integer) can end up as the reference target in very many distinct syntax spots within the total definition of the `SELECT` statement, and of other statements.

A [_syntax rule_](#syntax-rule) is specified using EBNF notation. EBNF stands for "extended Backus–Naur form". See this [Wikipedia article](https://en.wikipedia.org/wiki/Extended_Backus–Naur_form). Here is an example for the [`PREPARE`](/preview/api/ysql/the-sql-language/statements/perf_prepare/#syntax) statement:

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

### Diagram definition file

```output
<your path>/yugabyte-db/docs/content/preview/api/ysql/syntax_resources/ysql_grammar.ebnf
```

Located directly on the [_syntax resources directory_](#syntax-resources-directory), this file, uniquely under the whole of the [_ysql directory_](#ysql-directory) is not a `.md` file. And uniquely under the [_syntax resources directory_](#syntax-resources-directory), it is typed up humanly. It holds the definition, written in EBNF notation, of every [_syntax rule_](#syntax-rule) that is processed by the [_diagram generator_](#diagram-generator).

The [_diagram generator_](#diagram-generator) doesn't care about the order of the rules in the file. But the order is reproduced in the [_grammar diagrams file_](#grammar-diagrams-file). This, in turn, reflects decisions made by authors about what makes a convenient reading order. Try to spot what informs the present ordering and insert new rules in a way that respects this. In the limit, insert a new rule at the end as the last new properly-defined rule and before this comment:

```ebnf
(* Supporting rules *)
```

### Syntax diagram

- **The result, in the published documentation, of a [_syntax rule_](#syntax-rule) that is contained in the [_diagram definition file_](#diagram-definition-file)**.

The [_syntax diagram_](#syntax-diagram) and the [_syntax rule_](#syntax-rule) bear a one-to-one mutual relationship.

The [Syntax section](/preview/api/ysql/the-sql-language/statements/perf_prepare/#syntax) of the account of the [`PREPARE`](/preview/api/ysql/the-sql-language/statements/perf_prepare/) statement provides a short, but sufficient, example. The [_syntax diagram_](#syntax-diagram) appears as a (member of a) tabbed pair which gives the reader the choice to see a [_syntax rule_](#syntax-rule) as either the "Grammar" form (in the syntax used in the PostgreSQL documentation and the documentation for several other databases) or the "Diagram" form (a so-called "railroad diagram" that again is used commonly in the documentation for several other databases).

### Docs directory

- **The directory within your local git of the entire documentation source (the [_humanly typed documentation source_](#humanly-typed-documentation-source), the [_free-standing generated grammar-diagram pairs_](#free-standing-generated-grammar-diagram-pair) and the [_diagram definition file_](#diagram-definition-file)) together with the [_supporting doc infrastructure_](#supporting-doc-infrastructure)**:

```sh
cd <your path>/yugabyte-db/docs
```

You install the [_diagram generator_](#diagram-generator) here. And you stand here when you [_run the generator_](#run-the-generator). You also typically stand here when you start "hugo"; and when you prepare for, and then do, a `git push` to your personal GitHub fork.

### Content directory

- **Everything under the [_docs directory_](#docs-directory) that is not [_supporting doc infrastructure_](#supporting-doc-infrastructure)**.

In other words, it holds the entire [_humanly typed documentation source_](#humanly-typed-documentation-source) (not just for YSQL), the [_free-standing generated grammar-diagram pairs_](#free-standing-generated-grammar-diagram-pair) and the [_diagram definition file_](#diagram-definition-file):

```sh
cd <your path>/yugabyte-db/docs/content
```

"hugo" uses the tree starting at the [_content directory_](#content-directory) to generate the documentation site (under the influence of the [_supporting doc infrastructure_](#supporting-doc-infrastructure)) as a set of static files.

Notice that, when your current directory is the [_docs directory_](#docs-directory), `git status` shows the paths of what it reports starting with the [_content directory_](#content-directory) like this:

```output
modified:   content/preview/api/ysql/syntax_resources/grammar_diagrams.md
modified:   content/preview/api/ysql/syntax_resources/ysql_grammar.ebnf
modified:   content/preview/api/ysql/the-sql-language/statements/cmd_do.md
```

Both the `/preview/` and the `/stable/` subtrees are direct children of the [_content directory_](#content-directory).

### YSQL directory

- **The directory tree that holds all the [_humanly typed documentation source_](#humanly-typed-documentation-source), the [_free-standing generated grammar-diagram pairs_](#free-standing-generated-grammar-diagram-pair), and the [_diagram definition file_](#diagram-definition-file) that jointly describe the YugabyteDB YSQL subsystem in the /preview/ tree**.

```sh
cd <your path>/yugabyte-db/docs/content/preview/api/ysql
```

### Supporting doc infrastructure

- **Everything on and under the [_docs directory_](#docs-directory) that is not on or under the [_content directory_](#content-directory)**.

This material is not relevant for the present account.

### Humanly typed documentation source

- _(with a few exceptions)_ **The set of ".md" files located under the [ysql directory](#ysql-directory)**.

_Singleton exception:_ The [_diagram definition file_](#diagram-definition-file), on the [_syntax resources directory_](#syntax-resources-directory), is a plain text file that is typed up manually.

_Exception class:_ The [_grammar diagrams file_](#grammar-diagrams-file) and the [_free-standing generated grammar-diagram pairs_](#free-standing-generated-grammar-diagram-pair). These are all located in the syntax resources directory tree. They are all `.md` files. And none is humanly typed. Do not manually edit any of these.

With just a few on the [_ysql directory_](#ysql-directory) itself, the [_humanly typed documentation source_](#humanly-typed-documentation-source) files are located in directory trees that start at the _ysql directory._ The top-of-section `_index.md` for the YSQL subsystem is among the few [_humanly typed documentation source_](#humanly-typed-documentation-source) files that are located directly on the [_ysql directory_](#ysql-directory).

### Syntax resources directory

```sh
cd <your path>/yugabyte-db/docs/content/preview/api/ysql/syntax_resources
```

With the one exception of the [_diagram definition file_](#diagram-definition-file), every file within the [_syntax resources directory_](#syntax-resources-directory) tree is generated.

### Generated documentation source

- **The set of files that the [_diagram generator_](#diagram-generator) produces**.

All are located within the [_syntax resources directory_](#syntax-resources-directory) tree. The set minimally includes the [_grammar diagrams file_](#grammar-diagrams-file). It also includes all of the [_free-standing generated grammar-diagram pairs_](#free-standing-generated-grammar-diagram-pair) from this. These will be included within ordinary content `.md` files by URL reference from the [_diagram inclusion HTML_](#diagram-inclusion-html) located in the ordinary content `.md` file that wants the [_syntax diagram_](#syntax-diagram).

Notice that there is no garbage collection scheme for unreferenced [_generated documentation source_](#generated-documentation-source) files. Content authors must do this task manually.

### Grammar diagrams file

```output
<your path>/yugabyte-db/docs/content/preview/api/ysql/syntax_resources/grammar_diagrams.md
```

The grammar [diagrams file](/preview/api/ysql/syntax_resources/grammar_diagrams/#abort) contains every [_syntax diagram_](#syntax-diagram) that is generated from all of the [_syntax rules_](#syntax-rule) that are found in the [_diagram definition file_](#diagram-definition-file).

### Generated grammar-diagram pair

- **The Markdown source that produce the result, in the human-readable documentation, that the user sees as a [_syntax diagram_](#syntax-diagram)**.

Use this term when you want to focus in the fact that a [_syntax diagram_](#syntax-diagram) is presented for the user as a tabbed "Grammar" and "Diagram" pair.

Always found in the [_grammar diagrams file_](#grammar-diagrams-file) (but, uniquely, not here as a tabbed pair). Optionally found as a [_free-standing generated grammar-diagram pair_](#free-standing-generated-grammar-diagram-pair).

### Free-standing generated grammar-diagram pair

- **Such a pair requests the [_diagram generator_](#diagram-generator) to populate them automatically with respectively the "grammar" and the "diagram" presentations of the set of [_syntax rules_](#syntax-rule) that each asks for**.

  They are defined by a pair of `.md` files with names that follow a purpose-designed syntax:

```output
<rule-name>[, <rule-name>, ...].grammar.md
               ~               .diagram.md
```

Each pair is initially created empty (for example using `touch`) to express the content author's desire to have a diagram set for a particular [_humanly typed documentation source_](#humanly-typed-documentation-source) file.

Such a pair should be placed in the exact mirror sibling directory, in the [_syntax resources directory_](#syntax-resources-directory) tree, of the directory where the [_humanly typed documentation source_](#humanly-typed-documentation-source) file that wants to include the _specified syntax_ diagram is located.

Here is an example. Suppose that the file `wants-to-include.md` wants to include the [_syntax diagram_](#syntax-diagram) with a rule set denoted by the appropriately spelled identifier `<rule set X>`. And suppose that the [_humanly typed documentation source_](#humanly-typed-documentation-source) file is here:

```output
<your path>/yugabyte-db/docs/content/preview/api/ysql                 /dir_1/dir_2/dir_3/wants-to-include.md
```

The white space between `/ysql` and `/dir_1` has been introduced as a device to advertise the convention. It doesn't exist in the actual file path.

The [_free-standing generated grammar-diagram pair_](#free-standing-generated-grammar-diagram-pair) _must_ be placed here.

```output
<your path>/yugabyte-db/docs/content/preview/api/ysql/syntax_resources/dir_1/dir_2/dir_3/<rule set X>.grammar.md
<your path>/yugabyte-db/docs/content/preview/api/ysql/syntax_resources/dir_1/dir_2/dir_3/<rule set X>.diagram.md
```

Suppose that a [_syntax rule_](#syntax-rule) includes a reference to another [_syntax rule_](#syntax-rule). If the referenced [_syntax rule_](#syntax-rule) is included (by virtue of the name of the _diagram-grammar file pair_) in the same [_syntax diagram set_](#syntax-diagram-set), then the name of the [_syntax rule_](#syntax-rule) in the referring [_syntax diagram_](#syntax-diagram) becomes a link to the [_syntax rule_](#syntax-rule) in that same [_syntax diagram set_](#syntax-diagram-set). Otherwise the generated link target of the referring rule is within the [_grammar diagrams file_](#grammar-diagrams-file). The way that this link is spelled depends on the location, within the [_ysql directory_](#ysql-directory) tree, of the `.md` file that includes the generated [_syntax diagram_](#syntax-diagram).

If you don't follow this rule, then (as long as you specify the right path to the [_free-standing generated grammar-diagram pair_](#free-standing-generated-grammar-diagram-pair) in the [_diagram inclusion HTML_](#diagram-inclusion-html), you _will_ get the diagram in the `wants-to-include.md`, just as you want it. But the links to diagrams in the [_grammar diagrams file_](#grammar-diagrams-file) for rules that are not defined in the same included diagram will be broken. You should therefore always check manually that these links work when you first confirm that you see the generated [_syntax diagram_](#syntax-diagram) that you want at the location that you want it. And you should check again just before creating a Pull Request.

{{< note title="The 'underscore-index.md' files are special." >}}
An ordinary humanly typed _.md_ file is, tautologically, considered to live on the directory where it's found. But an _\_index.md_ file is considered to live on the parent directory of the directory where it's found. You must take this into account when an _\_index.md_ file wants to include a **[_free-standing generated grammar-diagram pair_](#free-standing-generated-grammar-diagram-pair)**. You must correspondingly locate the referenced _\*grammar.md_ and _\*diagram.md_ files under the _syntax_resources_ directory in the parent "mirror" directory of where the referring _\_index.md_ file is located. 
{{< /note >}}

### Syntax diagram set

- **The set of [_syntax diagrams_](#syntax-diagram) specified by spelling `<rule-name>[ , ... ]` in the first part of the filenames of the [_free-standing generated grammar-diagram pair_](#free-standing-generated-grammar-diagram-pair)**.

### Diagram inclusion HTML

- **The HTML snippet that you include, and edit, in a [_humanly typed documentation source_](#humanly-typed-documentation-source) file in which you want to include a [_free-standing generated grammar-diagram pair_](#free-standing-generated-grammar-diagram-pair)**.

Consider this example:

```output
<your path>/yugabyte-db/docs/content/preview/api/ysql/dir_1/dir_2/dir_3/wants-to-include.md
```

You must include this boilerplate text in `wants-to-include.md` at the location in this file where you want the [_syntax diagram_](#syntax-diagram) to be seen:

```html
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
  {{%/* includeMarkdown "../../../syntax_resources/dir_1/dir_2/dir_3/<rule set X>.grammar.md" */%}}
  </div>
  <div id="diagram" class="tab-pane fade show active" role="tabpanel" aria-labelledby="diagram-tab">
  {{%/* includeMarkdown "../../../syntax_resources/dir_1/dir_2/dir_3/<rule set X>.diagram.md" */%}}
  </div>
</div>
```

You take responsibility for spelling this

```output
../../../syntax_resources/dir_1/dir_2/dir_3/<rule set X>.grammar.md
```

and this:

```output
../../../syntax_resources/dir_1/dir_2/dir_3/<rule set X>.diagram.md
```

properly. The rest is entirely boilerplate. You musn't touch it. The construct `../../../` must climb up the proper number of levels (three in this example) to reach the parent [_ysql directory_](#ysql-directory) from `wants-to-include.md`.

Notice these two matching tag pairs:

- _\<a href="#grammar" ..._ with _\<div id="grammar" ..._
- _\<a href="#diagram" ..._ with _\<div id="diagram" ..._

Sometimes, you'll want to include more than one [_free-standing generated grammar-diagram pair_](#free-standing-generated-grammar-diagram-pair) in a particular [_humanly typed documentation source_](#humanly-typed-documentation-source) `.md` file. In such a case, you must use differently-spelled matching tag pairs for each successive diagram. The convention is to use these pairs for the second grammar-diagram pair:

- _\<a href="#grammar-2" ..._ with _\<div id="grammar-2" ..._
- _\<a href="#diagram-2" ..._ with _\<div id="diagram-2" ..._

and to use these for the third diagram pair:

- _\<a href="#grammar-3" ..._ with _\<div id="grammar-3" ..._
- _\<a href="#diagram-3" ..._ with _\<div id="diagram-3" ..._

and so on. If you do this, then _all_ diagram-pairs on the rendered page will switch in lock-step between the _"Grammar"_ tab and the _"Diagram"_ tab when you click the tab to make your choice using _any one_ of the pairs. Failure to follow this rule will mean that the syntax tabs don't display correctly.

### Diagram generator

- **The program that reads the [_diagram definition file_](#diagram-definition-file) and that repopulates the [_grammar diagrams file_](#grammar-diagrams-file) and the set of existing [_free-standing generated grammar-diagram pairs_](#free-standing-generated-grammar-diagram-pair) for every rule that the [_diagram definition file_](#diagram-definition-file) defines**.

It's implemented by `rrdiagram.jar` on the [_docs directory_](#docs-directory).

Here are the download instructions. You need to do this just once in a newly created local git (typically created from your personal fork of the master YB repo).

```sh
cd yugabyte-db/docs/

wget $(curl -s https://api.github.com/repos/Yugabyte/RRDiagram/releases/latest \
       | grep browser_download_url | cut -d \" -f 4)
```

Use `ls rrdiagram.jar` to check that it is now present. (It isn't shown by "git status" 'cos it's explicitly excluded.)

### Run the generator

- **first "cd..." then "java..."**

Specifically:

```sh
cd yugabyte-db/docs
java -jar rrdiagram.jar content/preview/api/ysql/syntax_resources/ysql_grammar.ebnf content/preview/api/ysql/syntax_resources/
```

This will (re)generate _all_ of the files that it ought to. You can run this at any time. In the worst case, a typing error somewhere, especially in the [_diagram inclusion HTML_](#diagram-inclusion-html), can crash hugo, resulting in notorious _Hugo black screen_ in the browser. This point is discussed, and  illustrated with examples, at _Step 4_ in the **[Example use case](#example-use-case)** section at the end.

> **Note:** To see help, run `java -jar rrdiagram.jar` (without arguments).

### About the redundant use of ( ... )

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

### Example use case

Suppose that this [_humanly typed documentation source_](#humanly-typed-documentation-source) file:

```output
<your path>/yugabyte-db/docs/content/preview/api/ysql/exprs/window_functions/window-definition.md
```

wants to include a [_syntax diagram set_](#syntax-diagram-set) with these [_syntax rules_](#syntax-rule):

> `frame_clause`, `frame_bounds`, and `frame_bound`

_Step 1:_ If they all already exist in the [_diagram definition file_](#diagram-definition-file), then go to _Step 2_. Else, type up any rules that don't yet exist.

_Step 2:_ Create these empty files (for example, with `touch`).

```sh
touch yugabyte-db/docs/content/preview/api/ysql/syntax_resources/exprs/window_functions/frame_clause,frame_bounds,frame_bound.diagram.md

touch yugabyte-db/docs/content/preview/api/ysql/syntax_resources/exprs/window_functions/frame_clause,frame_bounds,frame_bound.grammar.md
```

_Step 3:_ Copy a reliable example of the [_diagram inclusion HTML_](#diagram-inclusion-html) and paste it into `window-definition.md` at the location where you want the syntax diagram set to appear. Edit the bolded text shown above appropriately.

_Step 4:_ [_Run the generator_](#run-the-generator). This will generate a report on _stdout_. You must inspect this after every run and you should ensure that it is "clean" in that it looks like this:

```
INFO: Re-generating diagram file call_procedure,subprogram_arg.diagram.md
INFO: Re-generating grammar file call_procedure,subprogram_arg.grammar.md
INFO: Re-generating diagram file group_by_clause,grouping_element.diagram.md
INFO: Re-generating grammar file group_by_clause,grouping_element.grammar.md
INFO: Re-generating diagram file having_clause.diagram.md
INFO: Re-generating grammar file having_clause.grammar.md
...
INFO: Re-generating diagram file subprogram_call_signature.diagram.md
INFO: Re-generating grammar file subprogram_call_signature.grammar.md
INFO: Re-generating diagram file subprogram_signature,arg_decl,arg_name,arg_mode,arg_type.diagram.md
INFO: Re-generating grammar file subprogram_signature,arg_decl,arg_name,arg_mode,arg_type.grammar.md
INFO: Re-generating diagram file unalterable_fn_attribute,unalterable_proc_attribute.diagram.md
INFO: Re-generating grammar file unalterable_fn_attribute,unalterable_proc_attribute.grammar.md
WARNING: Ignoring file '/Users/Bllewell/YB-github-repos/yugabyte-db/docs/content/preview/api/ysql/syntax_resources/ysql_grammar.ebnf'. 
```

In other words, you must ensure that there are no reported errors and just the exactly one reported warning, as shown here. It helps to contrive some example errors, in turn, so that you'll be able immediately to recognize the cause when you get one (as is pretty much guaranteed to happen from time to time).

**Contrived error #1—forget the single-quotes around a keyword:** Look for this syntax rule in the [_diagram definition file_](#diagram-definition-file):

```
declare = 'DECLARE' cursor_name [ 'BINARY' ] [ 'INSENSITIVE' ] [ [ 'NO' ] 'SCROLL' ] \
          'CURSOR' [ ( 'WITH' | 'WITHOUT' ) 'HOLD' ] 'FOR' subquery ;
```

Remove the single quotes that surround `'INSENSITIVE'`. This will change its status in EBNF's grammar from _keyword_ to _syntax rule_. Most people find it hard to spot such a typo just by proof-reading. Now re-run the diagram generator. You won't see any errors reported on _stderr_. But if you look carefully at the _stdout_ report, you'll see this warning:

```
WARNING: Undefined rules referenced in rule 'declare': [INSENSITIVE]
```

It's clear what it means. And you must fix it because otherwise a reader of the YSQL documentation will see a semantic error—and will wonder what on earth the _insensitive_ rule is. Fix the problem immediately, re-run the generator. Then see a clean report again. This exercise tells you that it's a very good plan to re-run the generator after every edit to any single grammar rule. You'll know what rule you just edited and so you'll immediately know where to look for the error.

**Contrived error #2—misspell a syntax rule:** Look for this in the [_diagram definition file_](#diagram-definition-file):

```
savepoint_rollback = ( 'ROLLBACK' ['WORK' | 'TRANSACTION' ] 'TO' [ 'SAVEPOINT' ] name ) ;
```

Edit it to change, say, the first `]` character to `}`. It's easy to do this typo because these two characters are on the same key and it's hard to see the difference when you use a small font. Now re-run the diagram generator. You'll see this on _stderr_:

```
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

```
WARNING: Exception occurred while exporting rule savepoint_rollback
WARNING: savepoint_rollback = 'ROLLBACK' [ 'WORK' | 'TRANSACTION' } 'TO' [ 'SAVEPOINT' ] name ) ; ...
```

You can see that the offending `}` is mentioned. Fix it immediately, re-run the generator, and then see a clean report again. Re-start Hugo. This exercise, too, tells you that you should re-run the generator after every edit to a grammar rule. Here too, you'll know what rule you just edited and so will immediately know where to look for the error.

**Contrived error #3—misspell the name of a [free-standing generated grammar-diagram pair](#free-standing-generated-grammar-diagram-pair):** Change directory thus:

```
cd <your path>/yugabyte-db/docs/content/preview/api/ysql/syntax_resources/the-sql-language/statements
```

(The path to _yugabyte-db_ will depend on how you designed your filesystem.) Then make sure that this pair exists:

```
ls *insert*
```

You should see these two files:

```
insert,returning_clause,column_values,conflict_target,conflict_action.diagram.md
insert,returning_clause,column_values,conflict_target,conflict_action.grammar.md
```

Contrive a deliberate typo, by replacing _clause_ with _clase_, thus:

```
mv insert,returning_clause,column_values,conflict_target,conflict_action.diagram.md \
   insert,returning_clase,column_values,conflict_target,conflict_action.diagram.md

mv insert,returning_clause,column_values,conflict_target,conflict_action.grammar.md \
   insert,returning_clase,column_values,conflict_target,conflict_action.grammar.md

ls *insert*
```

You'll see this:

```
insert,returning_clase,column_values,conflict_target,conflict_action.diagram.md
insert,returning_clase,column_values,conflict_target,conflict_action.grammar.md
```

Now re-run the diagram generator. You won't see any errors reported on _stderr_. But if you look at the _stdout_ report, you'll see that it ends with this error:

```
ERROR: Invalid target rule: returning_clase
```

This means that one of the free-standing generated grammar-diagram pairs asks to include the syntax for a rule that isn't defined in the **[diagram definition file](#diagram-definition-file)**. This error won't cause the _Hugo black screen_. But if you use your Browser to look at the file that attempts to include this diagram pair:

```
<your path>/yugabyte-db/docs/content/preview/api/ysql/the-sql-language/statements/dml_insert.md
```

_i.e._ the page for the _INSERT_ statement, you'll see that, while the _Grammar_ and _Diagram_ tabs are present, no syntax representations are shown. Re-instate the proper file names, re-run the generator, and then see a clean report again. Then look again at the page for the _INSERT_ statement. (You'll need to refresh it.) Now you'll see that the intended syntax representations are back in place.

_Step 5:_ Make sure that you inspect the result immediately in the "hugo" rendering of the [_humanly typed documentation source_](#humanly-typed-documentation-source) into which you just included the [_diagram inclusion HTML_](#diagram-inclusion-html).
