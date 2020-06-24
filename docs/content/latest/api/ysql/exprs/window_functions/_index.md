---
title: YSQL window functions
linkTitle: Window functions
headerTitle: Window functions
description: Window functions operate on a row set that's defined, in general, as an ordered window within a containing restriciton defined by a subquery. They calculate a value for each row in the window by consulting values from other rows in the window.
image: /images/section_icons/api/ysql.png
menu:
  latest:
    identifier: window-functions
    parent: api-ysql-exprs
aliases:
  - /latest/api/ysql/exprs/window_functions
isTocNested: true
showAsideToc: true
---

## Organization of the window functions documentation

If you are already familiar with window functions, then you can skip straight to the [syntax and semantics](./sql-syntax-semantics/) section or the section that lists all of [YSQL's window functions](./function-syntax-semantics/) and that links, in turn, to the definitive account of each function.

**Note:** Some relational database systems use the term _analytic function_ for what YSQL and PostgreSQL call a _window function_.

This page has only the present organization overview section and the [Synopsis](./#synopsis) section. The remainder of the pages are organized as follows:

### Informal overview of function invocation using the OVER clause: [here](./functionality-overview/)

Skip this section entirely if you are already familiar with window functions. It presents five code examples.

- Some examples show the use of window functions. A window function can be invoked _only_ in a special variant of a `SELECT` list item, in a subquery, in conjunction with the keyword `OVER`.

- Others show how an aggregate function, too, may be invoked in the special variant of a `SELECT` list item in conjunction with the keyword `OVER`. You are very likely to be familiar, already, with using an aggregate function to compute a single measure, over all the rows in a subset of those that a subquery's `WHERE` clause defines, by adding a `GROUP BY` clause immediately after the `WHERE` clause. You might not realize that an aggregate function can also be invoked in the same way that you invoke a window function.

The section focuses on the _effect_ that the code in each example has. While it does name these clauses and syntax rules:

- the `OVER` clause, the [**window_definition**](../../syntax_resources/grammar_diagrams/#window-definition), the `PARTITION BY` clause,  the window `ORDER BY` clause, and [**frame_clause**](../../syntax_resources/grammar_diagrams/#frame-clause)

it leaves their formal definitions to the [syntax and semantics](./sql-syntax-semantics/) section.

**Note:** Usually, a SQL statement or a clause within such a statement is referred to by the keyword that introduces it, like the `WHERE` clause or the `GROUP BY` clause. The term _"window `ORDER BY` clause"_ is used throughout the _"Window functions"_ major section to distinguish it from the regular `ORDER BY` clause that is used at the syntax spot that precedes the spot for the `LIMIT` clause at the end of a `SELECT` statement. Sometimes, critical clauses cannot be identified this way. For example, the [**frame_clause**](../../syntax_resources/grammar_diagrams/#frame-clause) must start with one of the keywords `RANGE`, `ROWS`, or `GROUPS`. Such a clause will be named, therefore, by the name of the SQL syntax rule that governs it. Every such rule (spelled in lower case with underscores) is specified on the [Grammar Diagrams](../../syntax_resources/grammar_diagrams/#abort) page. Search in the page for a rule name to see where it is defined and used.

### Window function invocation—SQL syntax and semantics: [here](./sql-syntax-semantics/)

This section presents the formal treatment of the syntax and semantics of how a window function, or an aggregate function, is invoked as a special kind of `SELECT` list item in conjunction with the `OVER` keyword.

### Signature and purpose of each window function: [here](./function-syntax-semantics/)

The following list groups YSQL's eleven window functions in the same way that the sidebar items group them. The rationale for the grouping is explained in the referenced section.

&#160;&#160;&#160;&#160;&#160;&#160;[`row_number()`](./function-syntax-semantics/row-number-rank-dense-rank/#row-number)<br>
&#160;&#160;&#160;&#160;&#160;&#160;[`rank()`](./function-syntax-semantics/row-number-rank-dense-rank/#rank)<br>
&#160;&#160;&#160;&#160;&#160;&#160;[`dense_rank()`](./function-syntax-semantics/row-number-rank-dense-rank/#dense-rank)

&#160;&#160;&#160;&#160;&#160;&#160;[`percent_rank()`](./function-syntax-semantics/percent-rank-cume-dist-ntile/#percent-rank)<br>
&#160;&#160;&#160;&#160;&#160;&#160;[`cume_dist()`](./function-syntax-semantics/percent-rank-cume-dist-ntile/#cume-dist)<br>
&#160;&#160;&#160;&#160;&#160;&#160;[`ntile()`](./function-syntax-semantics/percent-rank-cume-dist-ntile/#ntile)

&#160;&#160;&#160;&#160;&#160;&#160;[`first_value()`](./function-syntax-semantics/first-value-nth-value-last-value/#first-value)<br>
&#160;&#160;&#160;&#160;&#160;&#160;[`nth_value()`](./function-syntax-semantics/first-value-nth-value-last-value/#nth-value)<br>
&#160;&#160;&#160;&#160;&#160;&#160;[`last_value()`](./function-syntax-semantics/first-value-nth-value-last-value/#last-value)

&#160;&#160;&#160;&#160;&#160;&#160;[`lag()`](./function-syntax-semantics/lag-lead/#lag)<br>
&#160;&#160;&#160;&#160;&#160;&#160;[`lead()`](./function-syntax-semantics/lag-lead/#lead)

&#160;&#160;&#160;&#160;&#160;&#160;[The data sets used by the code examples](./function-syntax-semantics/data-sets/)

### Analyzing a normal distribution with percent_rank(), cume_dist() and ntile(): [here](./analyzing-a-normal-distribution/)

Regard this section as an optional extra. It answers an interesting question:

- If you want to allocate a row set into buckets where each contains the same number of rows, based on your ordering rule, is there any difference between the result produced by using, in turn, each of the three functions `percent_rank()`, `cume_dist()`, or `ntile()`?

The answer is, of course, "Yes". But it's a qualified "Yes" because when certain conditions hold, there is _no_ difference. The value that the study brings is due to the fact that it aims to meet a high-level goal rather than to demonstrate the basic use of low-level primitives. This means that it necessarily combines tha basic use of the window functions under study with all sorts of other generic SQL techniques (and even stored procedure techniques) because these are needed to meet the goal. This kind of bigger-picture use typically goes hand-in-hand with any serious use of window functions.

Here is the problem statement:

- A row set with _N_ rows has only unique values of the column list that the `OVER` clause specifies for ordering the rows in the [_window_](./sql-syntax-semantics/#the-window-definition-rule). (In other words, there are no ties.)
- The aim is to allocate the rows into _n_ buckets that each have the same number of rows.
- _N_ is an integral multiple of _n_.

It shows that the exact same bucket allocation is produced when you use any one of the three functions under study. And it shows that if either of those special qualifications isn't met (in other words, _either_ if there are ties, _or_ if _N_ is not an integral multiple of _n_), then the bucket allocations differ.

## Synopsis

A window function operates, in general, on each of the set of [_windows_](./sql-syntax-semantics/#the-window-definition-rule) into which the rows of its input row set are divided, and it produces one value for every row in each of the [_windows_](./sql-syntax-semantics/#the-window-definition-rule). A [_window_](./sql-syntax-semantics/#the-window-definition-rule) is defined by having the same values for a set of one or more classifier columns. In the limit, there will be just a single [_window_](./sql-syntax-semantics/#the-window-definition-rule). In general, the output value of a window function for a particular row takes into account values from all of the rows in the successive [_windows_](./sql-syntax-semantics/#the-window-definition-rule) on which it operates.

A window function can be invoked only in a select list item in a subquery; and specific syntax, using the `OVER` clause is essential. The argument of this clause is the [**window_definition**](../../syntax_resources/grammar_diagrams/#window-definition) . The authoritative account is given in the section [Window function invocation—SQL syntax and semantics](./sql-syntax-semantics). Here is a brief overview. A window definition can have up to three clauses, thus:

- The `PARTITION BY` clause. This specifies the rule that divides the input row set into two or more [_windows_](./sql-syntax-semantics/#the-window-definition-rule). If it's omitted, then the input row set is treated as a single [_window_](./sql-syntax-semantics/#the-window-definition-rule).
- The window `ORDER BY` clause. This specifies the rule that determines how the rows within each [_window_](./sql-syntax-semantics/#the-window-definition-rule) are ordered. If it's omitted, then the ordering is unpredictable—and therefore the output of the window function is meaningless.
- The [**frame_clause**](../../syntax_resources/grammar_diagrams/#frame-clause). This is significant for only some of the eleven window functions. It determines which rows within each [_window_](./sql-syntax-semantics/#the-window-definition-rule) are taken as the input for the window function. For example, you can use it to exclude the current row; or you can say that the function should look only at the rows in the [_window_](./sql-syntax-semantics/#the-window-definition-rule) from the start through the current row.

Examples of each of these clauses are shown in the section [Informal overview of function invocation using the OVER clause](./functionality-overview).

Window functions are similar to aggregate functions in this way:

- Each operates on each of possibly many [_windows_](./sql-syntax-semantics/#the-window-definition-rule) of a row set.

**Note:** As mentioned above, the row set for a window function can be defined _only_ by using the `PARTITION BY` clause within a [window_definition](../../syntax_resources/grammar_diagrams/#window-definition) . The row set for an aggregate function _may_ be defined in this way. But it may alternatively be defined by the regular `GROUP BY` clause at the syntax spot in a subquery that follows the `WHERE` clause's spot.

Window functions differ from aggregate functions in this way:

- A window function produces, in general, a different output value for _each different input row_ in the [_window_](./sql-syntax-semantics/#the-window-definition-rule).
- An aggregate function, when it's invoked in the same way as a window function, always produces the _same value_ for _each different input row_ in the [_window_](./sql-syntax-semantics/#the-window-definition-rule).
- When an aggregate function is invoked using the regular `GROUP BY` clause, it produces a _single value_ for each whole [_window_](./sql-syntax-semantics/#the-window-definition-rule) that the `GROUP BY` clause defines.

