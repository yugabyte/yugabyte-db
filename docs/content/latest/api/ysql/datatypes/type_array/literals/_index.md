---
title: Create an array value using a literal
linkTitle: Literals
headerTitle: Create an array value using a literal
description: Bla bla
image: /images/section_icons/api/ysql.png
menu:
  latest:
    identifier: array-literals
    parent: api-ysql-datatypes-array
    weight: 20
isTocNested: false
showAsideToc: false
---

## The spelling of a literal value, alone, typically doesn't establish its data type

You will already be familiar with the notion of a literal as a way to represent a specific value of some particular data type. A literal can be used at any syntax spot, in YSQL and in PL/pgSQL, where an expression is legal. Often, the spelling of the literal isn't sufficient, alone, to establish the data type of the value—especially in YSQL where any value can be typecast to a `text` value. In fact, many typecasts are done implicitly, driven by the context of usage. However, it's good practice to know what you're doing (as it _always_ is) and to make all typecasts explicit by spelling out the typecast. Here is an example and a counter-example:

```postgresql
create table t(k serial primary key, v numeric);

prepare good(numeric) as insert into t(v) values ($1::numeric);
execute good(17.8::numeric);

prepare bad(int) as insert into t(v) values ($1);
execute bad(42.7);

select k, v from t order by k;
```

Here is the result:

```
 k |  v   
---+------
 1 | 17.8
 2 |   43
```

The convention used by _ysqlsh_, inherited from PostgreSQL's _psql_, is to display selected `numeric` values with exactly, and only, sufficient precision. So we see that `42.7` was implicitly typecast to the nearest `int` value, `43` by the invocation of `execute bad()` and then typecast back to the numeric value `43.0`on inserting it into the target column—to be displayed on selecting it as bare `42`. This is a telling example of a confusing, and therefore bad, practice.

We shall see that explicit typecasting is critically important when array literals are used.

## Array literals for array datatypes with primitive, and then compound, values

These three examples introduce the topic informally. First an array of primitive `int` values:
```postgresql
\t on
select '{1, 2, 3}'::int[];
```
The `\t on` metacommand supresses column headers and the rule-off unders these. Unless the headers are important for understanding, query output from _ysqlsh_ will be shown, throughout the present "arrays" major section, without these.

This is the output that the first example produces:

```
 {1,2,3}
```
The second example surrounds the values that the array literal defines with double quotes:
```postgresql
select '{"1", "2", "3"}'::int[];
```
It produces the identical output to the first example, where no double quotes were used.

The third example defines an array whose values are instances of a _"row"_ type:
```postgresql
create type rt as (a int, b int);

select '{"(1, 2)", "(3, 4)", "(5, 6)"}'::rt[];
```
It procudes this output:
```
 {"(1,2)","(3,4)","(5,6)"}
```
All whitespace has been removed. But the double quotes are retained. This suggests that they are significant. Test this by removing them. It causes the _"22P02: malformed row literal"_ error.

We can see, then, that when an array literal is used in a SQL statement, it must be quoted (using either single quotes or dollar quotes), just as is the case for a primitive `text` literal. Within the quotes, it starts with the left curly brace and ends with the right curly brace. And the closing quote is followed by a typecast to the appropriate array type. Within the curly braces, the values that the literal defines are delimited by commas, can always be surrounded by double quotes, sometimes need not be so surrounded, and sometimes must be.

The identical rules that apply for forming an array literal for use in a SQL statement apply when an array literal is used in an expression in a PL/pgSQL program.

The following subsections will present the rules carefully and, when the rules allow some freedom, will give recommendations. These rules are covered in this two sections of the PostgreSQL documentation:

- [8.15. Arrays](https://www.postgresql.org/docs/11/arrays.html)

- [8.16. Composite Types](https://www.postgresql.org/docs/11/rowtypes.html)

The [first subsection](./array-of-primitive-values/) gives the rules for array literals whose values are scalars (i.e. are of primitive data types).  

The [second subsection](./importance-of-typecasting/) revisits the crtically important topic of typecasting.

The [third subsection](./row/) gives the rules for the literal for a value of a _"row"_ type. These rules are essential to the understanding of  the next section.

The [fourth subsection](./array-of-rows/) gives the rules for array literals whose values are compound (i.e. are of row data types).



