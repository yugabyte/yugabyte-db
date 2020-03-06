---
title: Functions and operators by purpose
linktitle: Functions and operators by purpose
summary: Functions and operators by purpose
description: Functions and operators by purpose
menu:
  latest:
    identifier: functions-operators
    parent: functions-operators
isTocNested: true
showAsideToc: true
---

Four tables list the operators and functions by category, thus:

- [Create a JSON value from SQL values](../json-from-json/)
- [Create a JSON value from an existing JSON value](../json-from-json/)
- [Create a SQL value from a JSON value](../sql-from-json/)
- [Get a property of a JSON value](../get-property/)

There are two trivial typecast operators for converting between a `text` value that conforms to RFC 7159 and a `jsonb` or `json` value, the ordinarily overloaded `=` operator,  12 dedicated JSON operators and 23 dedicated JSON functions.

Most of the are overloaded so that they can be used on both `json` and `jsonb` values. When such an operator reads a subvalue as genuine JSON value then the result has the same data type as the input. When such an operator reads a subvalue as a SQL `text` value that represents the JSON value, then the result is the same for a `json` input as for a `jsonb` input.

Some of the functions have just a `jsonb` variant and a couple have just a `json` variant. Function names reflect this by starting with `jsonb_` or ending with `_jsonb` (and correspondingly for the `json` variants). This naming scheme might seem to reflect a strange design choice by the implementers of PostgreSQL. A trivial test shows that an overload pair _can_ be distinguished by the difference in formal parameter data type between `jsonb` and `json`. However, PostgreSQL doesn't allow an overload function pair to be distinguished by the difference in return data type. It seems, then, that the PostgreSQL implementers decided to use a single consistent naming convention—a `b` variant and a plain variant—both when the pair differ in the data type of the _input_ JSON value and when they differ in the data type of the _output_ JSON value. YSQL necessarily follows the PostgreSQL convention.

When an operator or function has both a JSON value input and a JSON value output, the `jsonb` variant takes a `jsonb` input and produces a `jsonb` output; and, correspondingly, the `json` variant takes a `json` input and produces a `json` output. You can use the `\df` _ysqlsh_ metacommand to show the signature (i.e. the data types of the formal parameters and the return value) of any of the JSON functions; but you cannot do this for the operators.

Check the full account of each to find its variant status. When an operator or function has both a `jsonb` and `json` variant, then only the `jsonb` variant is described. The functionality of the `json` variant can be trivially understood from the account of the `jsonb` functionality.

To avoid clutter in the tables, only the `jsonb` variants of the function names are mentioned except where only a `json` variant exists.

### Note about the code examples

The functionality of each operator and function is illustrated by a code example of this form:

```
[operator or function]
  acts on
[argument (list)]
  to produce
[result value]
```

It's insufficient simply to present this as the text representation of the relevant values because this loses data type information. Evening using SQL at the _ysqlsh_ prompt is like this:

```postgresql
[this SQL statement]
```

produces

```
[this result]
```

is insufficient for four reasons:

- It's hard to establish and advertise data type information without inserting into appropriately defined table columns. This brings distracting verbosity.
- The only way to build an expression from subexpressions, in pursuit of clarity, is to use scalar subqueries, named in a `with` clause. This, again, is so verbose that it obscures, rather than helps, clarity.
- This insufficiency is especially bothersome when the aim is to show how the output of a particular function depends upon the choice of value for an optional `boolean` parameter that conditions is behavior using, therefore, the same input JSON value in two function invocations.
- The result is non-negotiably typecast to text to print to the screen, with distracting conventions like inserting a space at the start of each printed line, showing the `boolean` value `true` as the `text` value `t`, showing `null` as just an absence, and showing a newline as the `text` value `+`.

For these reasons, each code example is presented as a `DO` block with this pattern:

- Each input value is declared using the appropriate SQL data type (sometimes building such values bottom-up from declared simpler values).
- The output of the operator or expression is assigned to a variable of the appropriate data type.
- The expected output is declared as a value of the same data type.
- An `assert` is used to show that the produced value is equal to the expected value, using an `is null` comparison where appropriate.

##### Note about SQL array manifest constants

RFC 7159 defines the syntax for a JSON _array_ as a comma-separated list of items surrounded by `[]` and  the syntax for a JSON _object_ as a comma-separated list of key-value pairs surrounded by `{}`. SQL defines one form for an array manifest constant as a `text` value with an inner syntax: the value starts with `{` and ends with `}` and contains a comma-separated list whose items are not themselves quoted but are all taken to be values of the array's data type. So this SQL manifest constant:

```
array['a', 'b', 'c']::text[]
```

can also be written thus:

```
'{a, b, c}'::text[]
```

This dramatic context-sensitive difference in meaning of `'{...}'` might confuse the reader. Therefore, in the major section _"JSON data types and functionality"_,  the `array[...]` form will be used for a SQL array manifest constant — and the `'{...}'`form will be avoided.

The fact that a JSON array can have subvalues of mixed data type but a SQL array can have only elements of the same data type means that special steps have to be taken when the goal is to construct a JSON array mixed subvalue data type from SQL values.
