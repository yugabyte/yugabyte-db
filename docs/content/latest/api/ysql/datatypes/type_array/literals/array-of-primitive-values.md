---
title: The literal for an array of primitive values
linkTitle: Array of primitive values
headerTitle: The literal for an array of primitive values
description: Bla bla
image: /images/section_icons/api/ysql.png
menu:
  latest:
    identifier: array-of-primitive-values
    parent: array-literals
    weight: 10
isTocNested: false
showAsideToc: false
---

## Multidimensional array of numeric values
You represent successive values in this kind of array (`numeric[]`, `int[]`, and so on) using the same convention that the primitive values of these types use.

```postgresql
create table t(k serial primary key, v int[]);

-- Insert a 1-dimensional int[] value.
insert into t(v) values('
    {1,  2}
  '::int[]);

-- Insert a 2-dimensional int[] value.
insert into t(v) values('
    {
      {1,  2},
      {3,  4}
    }
  '::int[]);

-- Insert a 3-dimensional int[] value.
insert into t(v) values('
    {
      {
        {1,  2}, {3,  4}
      },
      {
        {5,  6}, {7,  8}
      }
    }
  '::int[]);

-- Insert a 3-dimensional int[] value, specifying
-- the lower and upper bounds along each dimension.
insert into t(v) values('
    [3:4][5:6][7:8]=
    {
      {
        {1,  2}, {3,  4}
      },
      {
        {5,  6}, {7,  8}
      }
    }
  '::int[]);

select k, v::text from t order by k;
```
Notice that the three different `insert` statements define arrays with different dimensionality, as the comments state. This illustrates what was explained in the _"[Synopsis](../#synopsis)"_ section: the column `t.v` can hold array values of _any_ dimensionality.

Here is the `select` result:

```
 1 | {1,2}
 2 | {{1,2},{3,4}}
 3 | {{{1,2},{3,4}},{{5,6},{7,8}}}
 4 | [3:4][5:6][7:8]={{{1,2},{3,4}},{{5,6},{7,8}}}
```
Notice that whitespace in the inserted literals is insignificant, and that the `text` typecasts use whitespace (actually, the lack thereof) conventionally.

The result of typecasting the value of an array, a _"row"_ type value, and in fact the value of _any_ data type with `::text` can always be used as the literal to re-establish the starting value. In general, you need to surround the literal with single quotes (or, equivalently, with dollar quotes) to use it in a SQL statement or a PL/pgSQL program. But you can (and should) use the literals for numeric data types without surrounding quotes. By extension, the same recommendation applies to the representations of primitive numeric values within an array literal.

We shall refer to the result of the `::text` typecast of the value of any datatype as the _canonical representation_ of the literal that creates that value.

Notice the spelling of the array literal for the row with `k = 4`. The optional syntax `[3:4][5:6][7:8]` specifies the lower and upper bounds, respectively, for the first, the second, and the third dimension. (This is the same syntax that you use to specify a slice of an existing array.) When the freedom to specify the bounds is not exercised, then they are assumed all to start at `1`, and the canonocal form of the literal does not show the bounds.

When the freedom is exercised, the bounds for _every_ dimension must be specified. Specifying the bounds gives you, of course, an opportunity for error. If the length along each axis that you (implicitly) specify doesn't agree with the lengths that emerge from the actual values listed between the surrounding outer `{}` pair, then you get the _"22P02 invalid_text_representation"_  error with this prose explanation:

```
malformed array literal...
Specified array dimensions do not match array contents.
```
**Note:**

Try this:
```postgresql
select 12512454.872::text;
```
The result is the canonical form, `12512454.872`. So this (though you rarely see it):
```postgresql
select 12512454.872::numeric;
```
runs without error. Now try this:

```postgresql
select to_number('12,512,454.872', '999G999G999D999999')::text;
```
This, too, runs without error because it uses the `to_number()` builtin function. The result here, too, is the canonical form, `12512454.872`—with no commas. Now try this:

```postgresql
select '12,512,454.872'::numeric;
```
This causes the _"22P02: invalid input syntax for type numeric"_ error. In other words, _only_ a `numeric` value in canonical form can be directly typecast using `::numeric`.

You must spell the representations for the values in a `numeric[]` array in canonical form. Try this:

```postgresql
select ('{123.456, -456.789}'::numeric[])::text;
```
It shows this:

```
 {123.456,-456.789}
```

Now try this:
```postgresql
select ('{9,123.456, -8,456.789}'::numeric[])::text;
```
It silently produces this presumably unintended result—an array of _four_ numeric values—because the commas are taken as delimiters and not as part of the representation of a single `numeric` value:
```
 {9,123.456,-8,456.789}
```
In an array literal (or in a _"row"_ type value"_ type value literal), there is simply no way to accommodate forms that cannot be directly typecast. (The same holds for `timestamp` values as for `numeric` values.) SQL inherits this limitation from PostgreSQL. It is the user's responsibility to work around this when preparing the literal because, of course, functions like _"to_number()"_ cannot be used within literals. They can, however, be used in an `array[]` value constructor as [the section on that topic](../../constructors/) shows.

## One-dimensional array of boolean values

To understand how arrays of the remaining primitive data types should be written, it's sufficient to consider the one-dimensional case. The understanding generalizes to the multidimensional case.

The literals for the `boolean` data type  are `t` and `f` (which must be quoted for use). In contrast, `true` and `false` are reserved words in both SQL and PL/pgSQL. But PostgreSQL, and therefore YSQL, are forgiving. Try this:

```postgresql
select true,  't'::boolean, 'true'::boolean,
       false, 'f'::boolean, 'false'::boolean;
```
It shows this:
```
 t    | t    | t    | f    | f    | f
```
_ysqlsh_ (inheriting this behavior from _psql_) shows boolean values using the `t` or `f` representations. But a `boolean` value that is explicitly typecast to `::text` is displayed as `true`. The result of the following two maximally terse queries is consistent with this rule. First this:
```postgresql
select true;
```
and then this:
```postgresql
select true::text;
```
The first example shows `t` and the second example shows `true`.

This example cuts _ysqlsh_ out of the picture and uses a PL/pgSQL `assert` thus:

```postgresql
do $body$
declare
  t1 constant boolean := true;
  t2 constant boolean := 't'::boolean;
  t3 constant boolean := 'true'::boolean;
begin
  assert
    t1 and t2 and t3 and (t1::text = 'true'),
  'assert failed';
end;
$body$;
```
We shall see in the next sections that `null` is even more special. It has no canonical literal. You can specify `null` for a primitive data type only by using the reserved word `null`. The same holds for an array literal. But you can specify it for a field in a _"row"_ type value"_  _only_ by leaving no whitespace between the successive delimiters for that field.

Here, then, is how the literal for a `boolean` array should be written:
```postgresql
select ('{t,  f}'::boolean[])::text;
```
However, it _may_ be written thus:
```postgresql
select ('{true,  false}'::boolean[])::text;
```
reflecting the same forgiveness that was mentioned above. Each of these two queries shows the same result:
```
 {t,f}
```
Here's how to convince yourself that you really did get the values that you intended to set:

```postgresql
with
  v as (select '{t, f}'::boolean[] as a)
select a[1], a[2] from v;
```
It shows this:
```
 t | f
```
## One-dimensional array of timestamp values

First recap how the literal for a primitive `timestamp` value can be written:
```postgresql
select ('   2017-07-04   11:17:42   '::timestamp)::text;
```
It shows this:
```
 2017-07-04 11:17:42
```
However, you're very unlikely to see the freedom to use whitespace liberally, that this example shows, in real application code. While it doesn't confuse the parser, it will doubtless confuse the human reader. Yugabyte recommends against exploiting this freedom.

Now try this:

```postgresql
select (array[
    '2019-02-14 22:39:21'::timestamp,
    '2017-07-04 11:17:42'::timestamp
  ]::timestamp[])::text;
```

It shows this:

```
{"2019-02-14 22:39:21","2017-07-04 11:17:42"}
```
 We see from this that the canonical representation of a value within an array literal of type `timestamp[]`takes this form:

- The representation of each `timestamp` value has the form of the canonical representation of such a value, as produced by the `::timestamp` typecast of a primitive value, tightly surrounded by double quotes.

- There is no whitespace between the opening and closing curly braces and the comma delimiters and the double-quoted representations of the values that they delimit.

Apart from the fact that `timestamp` values are double-quoted, this is the same convention that the canonical forms of the literals for arrays of numeric values (`int[]`, `numeric[]`, and so on) and for `boolean` values use.

Now try this contrived example:
```postgresql
select (
  '{  2019-02-14   22:39:21 ,  2017-07-04 11:17:42 }'::timestamp[]
  )::text;
```
It produces the same  canonical representation  for the `timestamp[]` array that was produced when the array value was created using the `array[]` type constructor—the same text that inspired the present example:
```
 {"2019-02-14 22:39:21","2017-07-04 11:17:42"}
```

Notice that the surrounding double quotes that the canonical representation uses are not present in the input, and that whitespace has been used liberally to confuse the human reader between the enclosing `{}` pair. But the parser manages not to be confused because the delimiters (the `{` character, the `,` character, and the `}` character) are sufficient for its purposes. However, Yugabyte recommends against exploiting this freedom.

We see, then, that the fact that the canonical representation of a `timestamp[]` array surrounds the values with double quotes is the result of convention rather than necessity. When the values of a particular datatype have no interior spaces, they are not surrounded by double quotes. But when, like `timestamp` they do, then they are so surrounded. The same rule holds—more or less—for `text[]` arrays. But, as we shall see, there are some additional subtleties.

## One-dimensional array of text values

Try this:
```postgresql
select '>'||'   dog  house   '::text||'<';
```
It shows this:
```
 >   dog  house   <
```
This emphasises the fact that a `text` value—in contrast to values of non-stringy data types—can start and end with arbitrary numbers of spaces and can contain interior runs of spaces—and that these spaces are semantically significant. Now try this:
```postgresql
with
  v as (select '{a,   dog  house   ,b}'::text[] as a)
select '>'||a[1]||'<', '>'||a[2]||'<', '>'||a[3]||'<' from v;
```
It shows this:
```
 >a<      | >dog  house< | >b<
```
We see that, with no further punctuation, the text value starts with the first non-whitespace character after the comma delimiter and ends with the last non-whitespace character before the next comma delimiter. Use this query to see the canonical form of the literal for this array value:
```postgresql
select '{a,   dog  house   ,b}'::text[];
```
It shows this:
```
{a,"dog  house",b}
```
And now try this:
```postgresql
with
  v as (select '{"a", "   dog  house   ", "b"}'::text[] as a)
select '>'||a[1]||'<', '>'||a[2]||'<', '>'||a[3]||'<' from v;
```
It shows this:
```
 >a<      | >   dog  house   < | >b<
```
as presumably was intended. We see, therefore, that double quotes are essential around a string value that is intended to start or end with whitespace, that they may be used even around a single character where whitespace doesn't enter the picture, and that whitespace (as we already saw) between successive array elements following a delimiter (left curly brace or comma) and preceding the next delimiter (comma or right curly brace) is insignificant,

Here's an example from the following section on [literals for values of a _"row"_ type value](../row/):
```postgresql
create type rt as (f1 text, f2 text, f3 text);

with
  v as (select '(a,   dog  house   ,b)'::rt as r)
select '>'||(r).f1||'<', '>'||(r).f2||'<', '>'||(r).f3||'<' from v;
```
It shows this:
```
 >a<      | >   dog  house   < | >b<
```
This outcome might surprise you. The rules that explain why the outcomes of these two examples (array literal and _"row"_ type value literal) differ in this way are defined authoritatively in the PostgreSQL documentation [here](https://www.postgresql.org/docs/11/arrays.html) and [here](https://www.postgresql.org/docs/11/rowtypes.html). But `::text` typecasting the array value shows us the canonical form of the literal:
```postgresql
select ('(a,   dog  house   ,b)'::rt)::text;
```
It shows this:
```
 (a,"   dog  house   ",b)
```
We can understand the rules needed to write this form with far less effort than it takes to understand the full set of rules that predict that the examples above are legal and will produce the effects that are seen.

Finally, in the topic of the literal for a `text[]` array, we must consider the case that the desired value for an element looks like this:
```
She said "Write it like this '{1, 2}' (no \)."
```
This `text` value contains double quotes, a backslash, left and right curly braces, and a comma—all of which have defined punctuation roles in the grammar for an array literal. Further, the value contains single quotes—and this character has a defined punctuation role in the grammars for SQL and PL/pgSQL. The interior single quotes are handled by doubling them up, or, for better readability, by using dollar quotes to surround the text literal. And, for the rules that interpret a `text` value as an array literal, it is both necessary and sufficient to surround an element that is to be interpreted as a `text` value for the left and right curly braces, and the comma, to be treated as ordinary content. This is exactly analogous to how quoting a `text` literal in SQL and in PL/pgSQL shields characters within the value, such as `+`, `=`, `/`, and so on that normally have syntactic significance, from the parser. This leaves only the double quotes and the backslash needing special treatment. Surprisingly, doubling up a double quote doesn't work. The backslash escape technique, familiar from other programming languages, and from JSON, is the right approach for both of these troublesome characters.

Try this:
```postgresql
with v as (
  select
    $arr${x,"She said \"Write it like this '{1, 2}' (no \\).\""}$arr$::text[]
  as a)
select a[2] from v;
```
It shows the desired `text` value for the second element.

## NULLs in array literals

Try this, using the [array constructor](../../constructors):
```postgresql
select (array [42::int, null::int, 17::int]::int[])::text;
```
It produces this:

```
 {42,NULL,17}
```
This might a surprise. Here (and uniquely for the canonical representation of an array literal) `null` is represented by the character string `NULL` (it isn't case-sensitive). This contrast with how a primitive value (of any data type) that is `null` is represented in _ysqlsh_ output—in the same way that the empty string is represented. You might have guessed that this would work:
```postgresql
select '{42,,17}'::int[];
```
But it fails with the _"22P02: malformed array literal:"_ error.

## Always write array literals in canonical form

Bear in mind that you will very rarely manually type literals in the way that this section has used to demonstrate the rules. You'll do this only when teaching yourself, when prototyping new code, or when debugging. Rather, you'll typically create the literals programmatically—often in a client-side program that parses out the data values from, for example, an XML text file, or these days probably a JSON text file. In these scenarios, the target array is likely to have the data type `some_user_defined_row_type[]`. The rules for these kinds of array are described in the section [The literal for an array of _"row"_ type values](../array-of-rows/). Your program will parse the input and create the required literals as ordinary text strings that you'll then provide as the actual argument to a `prepare` statement execution, leaving the typecast of the `text` actual argument, to the appropriate array data type, to the prepared `insert` or `update` statement like this:

```
prepare stmt(text) as insert into t(rs) values($1::rt[]);
```
Example code to do this is presented in the section _"[Programmatic construction of the literal for an array of _"row"_ type values](../programmatic-literal-construction/)"_.

You can create any desired array value simply by using the canonical form of the literal. We have already seen many examples of this. And, as mentioned above, the set of rules that you need to understand how to write a literal in canonical form, to produce the value that you want, is considerably smaller than that of all the rules that prescribe how to write all the legal variants of array literals.

**Yugabyte recommends that the array literals that you generate programmatically are always spelled using the canonical representations**

You can relax this recommendation, to make tracing or debugging your code easier, by using a newline between each successive encoded value in the array.
