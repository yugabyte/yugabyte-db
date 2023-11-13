---
title: Subprogram attributes [YSQL]
headerTitle: Subprogram attributes
linkTitle: Subprogram attributes
description: Describes and categorizes the various attributes that characterize user-defined functions and procedures [YSQL].
image: /images/section_icons/api/subsection.png
menu:
  stable:
    identifier: subprogram-attributes
    parent: user-defined-subprograms-and-anon-blocks
    weight: 10
type: indexpage
showRightNav: true
---

The overall behavior of a user-defined function or procedure is determined by a set of characteristics that are defined with the _create [or replace]_ and _alter_ statements for each subprogram kind.

- The following characteristics are singletons in the categorization scheme in that they may be set _only_ with _create [or replace]_ and the rules that specify how they are set are not spelled with "attribute":

  - The argument list (i.e. the name, the mode, the data type, and optionally a default expression for each argument). See the [arg_decl_with_dflt](../../../ysql/syntax_resources/grammar_diagrams/#arg-decl-with-dflt) rule.

  - And, just for a function, the return data type.

- All of the other determining characteristics are known by the term of art _attribute_. This term is used in the names of rules in the [YSQL Grammar](../../syntax_resources/grammar_diagrams/) for distinct subcategories of attribute, grouped according to _when_ they may be set (only with _create [or replace]_, only with _alter_, or with both) and to _which kind_ of subprogram they apply (only to functions, or to both functions and procedures).

You can see the names of all of these rules in the grammars for _create [or replace] function_, _create [or replace] procedure_, _alter function_, and _alter procedure_, below:

{{%ebnf localrefs="alterable_fn_only_attribute,alterable_fn_and_proc_attribute,special_fn_and_proc_attribute,unalterable_fn_attribute,unalterable_proc_attribute" %}}
  create_function,
  create_procedure,
  alter_function,
  alter_procedure
{{%/ebnf%}}

Here are the different attribute rules.

## Unalterable subprogram attributes

The _unalterable subprogram attributes_ can be set _only_ with the _create [or replace]_ statement. Each of _function_ and _procedure_ has its own _unalterable attributes_ rule. They share _language_ and _subprogram_implementation_. But the status _regular function_ or _window function_ is meaningless for a procedure.

{{%ebnf%}}
  unalterable_fn_attribute,
  unalterable_proc_attribute
{{%/ebnf%}}

{{< note title="This major section, so far, describes only user-defined subprograms and anonymous blocks that are implemented in SQL or PL/pgSQL." >}}
Further, it does not yet describe how to create user-defined window functions.
{{< /note >}}


## Special subprogram attributes

The special subprogram attributes are set using a general syntax style with the _alter_ statements.

{{%ebnf%}}
  special_fn_and_proc_attribute
{{%/ebnf%}}

The syntax diagram shows that if you want to change any of these attributes, then you must change them one at a time by issuing _alter_ repeatedly.

The _schema_ and the _name_ of a subprogram are set using dedicated explicit syntax with _create [or replace]_. But the _owner_ cannot be explicitly set with _create [or replace]_; rather, a new subprogram's _owner_ is implicitly set to what the _[current_role](https://www.postgresql.org/docs/11/functions-info.html#FUNCTIONS-INFO-SESSION-TABLE)_ built-in function returns. (This will be what the _[session_user](https://www.postgresql.org/docs/11/functions-info.html#FUNCTIONS-INFO-SESSION-TABLE)_ built-in function returns if _create [or replace]_ is issued as a top-level SQL statement; and it will be the _owner_ of a _security definer_ subprogram that issues the SQL statement.)

As it happens, and just for PostgreSQL-historical reasons, if you want to specify the _extension_ on which a new subprogram depends you can do this only by _first_ creating the subprogram and _then_ specifying the name of the _extension_ using the subprogram-specific _alter_ statement.

See the section [The semantics of the "depends on extension" subprogram attribute](depends-on-extension-semantics/) for more information about this attribute.

## Alterable subprogram attributes

These attributes are common for both functions and procedures:

{{%ebnf%}}
  alterable_fn_and_proc_attribute
{{%/ebnf%}}

See the subsection [Alterable subprogram attributes](./alterable-subprogram-attributes/) for the explanations of the _configuration parameter_ and _security_ attributes.

## Alterable function-only attributes

Notice that there are no procedure-specific alterable attributes. These attributes are specific to just functions:

{{%ebnf%}}
  alterable_fn_only_attribute
{{%/ebnf%}}

See the subsection [Alterable function-only attributes](./alterable-function-only-attributes/) for the explanations of the _volatility_, _On NULL input_, _parallel_, _leakproof_, _cost_ and _rows_ attributes.
