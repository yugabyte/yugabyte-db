---
title: Subprogram attributes [YSQL]
headerTitle: Subprogram attributes
linkTitle: Subprogram attributes
description: Describes and categorizes the various attributes that characterize user-defined functions and procedures [YSQL].
image: /images/section_icons/api/subsection.png
menu:
  v2.14:
    identifier: subprogram-attributes
    parent: user-defined-subprograms-and-anon-blocks
    weight: 10
type: indexpage
showRightNav: true
---

The overall behavior of a user-defined function or procedure is determined by a set of characteristics that are defined with the `create [or replace]` and `alter` statements for each subprogram kind.

- The following characteristics are singletons in the categorization scheme in that they may be set _only_ with `create [or replace]` and the rules that specify how they are set are not spelled with "attribute":

  - The argument list (i.e. the name, the mode, the data type, and optionally a default expression for each argument--see the [arg_decl_with_dflt](../../../ysql/syntax_resources/grammar_diagrams/#arg-decl-with-dflt) rule)

  - And, just for a function, the return data type.

- All of the other determining characteristics are known by the term of art _attribute_. This term is used in the names of rules in the [YSQL Grammar](../../syntax_resources/grammar_diagrams/) for distinct subcategories of attribute, grouped according to _when_ they may be set (only with `create [or replace]`, only with `alter`, or with both) and to _which kind_ of subprogram they apply (only to functions, or to both functions and procedures).

You can see the names of all of these rules in the grammars for `create [or replace] function`, `create [or replace] procedure`, `alter function`, and `alter procedure`, below:

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/user-defined-subprograms-and-anon-blocks/create_function,create_procedure,alter_function,alter_procedure.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/user-defined-subprograms-and-anon-blocks/create_function,create_procedure,alter_function,alter_procedure.diagram.md" %}}
  </div>
</div>

Here are the different attribute rules.

## Unalterable subprogram attributes

The _unalterable subprogram attributes_ can be set _only_ with the `create [or replace]` statement. Each of _function_ and _procedure_ has its own _unalterable attributes_ rule. They share _language_ and _implementation_definition_. But the status _regular function_ or _window function_ is meaningless for a procedure.

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#grammar-2" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram-2" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar-2" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/user-defined-subprograms-and-anon-blocks/unalterable_fn_attribute,unalterable_proc_attribute.grammar.md" %}}
  </div>
  <div id="diagram-2" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/user-defined-subprograms-and-anon-blocks/unalterable_fn_attribute,unalterable_proc_attribute.diagram.md" %}}
  </div>
</div>

As of the current _latest_ version of YugabyteDB, user-defined subprograms can be implemented in SQL, PL/pgSQL, or C. (However, this section does not address implementing user-defined subprograms in C.)

See the section [PL/pgSQL](_to_do_) for an account of that language's syntax and semantics.

See the section [Window functions](../../exprs/window_functions/) for an account of this special kind of function.

## Special subprogram attributes

The special subprogram attributes are set using a general syntax style with the `alter` statements.

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#grammar-3" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram-3" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar-3" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/user-defined-subprograms-and-anon-blocks/special_fn_and_proc_attribute.grammar.md" %}}
  </div>
  <div id="diagram-3" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/user-defined-subprograms-and-anon-blocks/special_fn_and_proc_attribute.diagram.md" %}}
  </div>
</div>

The syntax diagram shows that if you want to change any of these attributes, then you must change them one at a time by issuing `alter` repeatedly.

The _schema_ and the _name_ of a subprogram are set using dedicated explicit syntax with `create [or replace]`. But the _owner_ cannot be explicitly set with `create [or replace]`; rather, a new subprogram's _owner_ is implicitly set to what the _[current_user](https://www.postgresql.org/docs/11/functions-info.html#FUNCTIONS-INFO-SESSION-TABLE)_ built-in function returns. (This will be what the _[session_user](https://www.postgresql.org/docs/11/functions-info.html#FUNCTIONS-INFO-SESSION-TABLE)_ built-in function returns if `create [or replace]` is issued as a top-level SQL statement; and it will be the _owner_ of a _security definer_ subprogram that issues the SQL statement.)

As it happens, and just for PostgreSQL-historical reasons, if you want to specify the _extension_ on which a new subprogram depends you can do this only by _first_ creating the subprogram and _then_ specifying the name of the _extension_ using the subprogram-specific `alter` statement.

See the section [The semantics of the "depends on extension" subprogram attribute](../depends-on-extension-semantics/) for more information about this attribute.

## Alterable subprogram attributes

These attributes are common for both functions and procedures:

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#grammar-4" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram-4" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar-4" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/user-defined-subprograms-and-anon-blocks/alterable_fn_and_proc_attribute.grammar.md" %}}
  </div>
  <div id="diagram-4" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/user-defined-subprograms-and-anon-blocks/alterable_fn_and_proc_attribute.diagram.md" %}}
  </div>
</div>

See the subsection [Alterable subprogram attributes](./alterable-subprogram-attributes/) for the explanations of the _configuration parameter_ and _security_ attributes.

## Alterable function-only attributes

Notice that there are no procedure-specific alterable attributes. These attributes are specific to just functions:

<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#grammar-5" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram-5" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar-5" class="tab-pane fade show active" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../syntax_resources/user-defined-subprograms-and-anon-blocks/alterable_fn_only_attribute.grammar.md" %}}
  </div>
  <div id="diagram-5" class="tab-pane fade" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../syntax_resources/user-defined-subprograms-and-anon-blocks/alterable_fn_only_attribute.diagram.md" %}}
  </div>
</div>

See the subsection [Alterable function-only attributes](./alterable-function-only-attributes/) for the explanations of the _volatility_, _On NULL input_, _parallel_, _leakproof_, _cost_ and _rows_ attributes.
