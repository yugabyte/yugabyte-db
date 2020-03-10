---
title: Code example conventions
linkTitle: Code example conventions
summary: Code example conventionss
description: JSON code example conventions
menu:
  latest:
    identifier: code-example-conventions
    parent: api-ysql-datatypes-json
    weight: 30
isTocNested: true
showAsideToc: true
---







## Note about the code examples

The functionality of each operator and function is illustrated by a code example of this form:

```
[function or operator]
  acts on
[argument (list)]
  to produce
[result value]
```

It's insufficient simply to present this as the text representation of the relevant values because this loses data type information. Evening using SQL at the _ysqlsh_ prompt is like this:

```
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

## Note about SQL array manifest constants

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