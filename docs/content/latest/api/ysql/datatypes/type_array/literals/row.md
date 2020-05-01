---
title: The literal for a row
linkTitle: Row
headerTitle: The literal for a "row" type value
description: The literal for a "row" type value
menu:
  latest:
    identifier: row
    parent: array-literals
    weight: 30
isTocNested: false
showAsideToc: false
---

The word "row" has two different uses—but these uses are really different sides of the same coin. A row in a schema-level table is actually an occurrence of a _"row"_ type—in other words, a _"row"_ type value. In this case, the schema-level _"row"_ type is created automatically as a side effect of executing the _"create table"_ statement. It has the same name as the table. (This is allowed because tables and types are in different namespaces.) Further, a column in a schema-level table can have a user-defined _"row"_ type as its data type, and in this case the _"row"_ type need not be partnered with a table.

You might see the term _"record"_ when you use the `\df` metacommand to show the signature of a function. Briefly, it's an anonymous _"row"_ type. You produce a record instance when you use a literal that has the correct form of a _"row"_ type but when you omit the typecast operator. If you adhere to recommended practice, and always explicitly typecast such literals, then you needn't try to understand what a record is.

You can read more about these notions in the PostgreSQL documentation here:

- section [43.3.4. Row Types](https://www.postgresql.org/docs/11/plpgsql-declarations.html#PLPGSQL-DECLARATION-ROWTYPES)

- Section [43.3.5. Record Types](https://www.postgresql.org/docs/11/plpgsql-declarations.html#PLPGSQL-DECLARATION-RECORDS)

We need first to understand how to write a literal for a _"row"_ type value before we can understand, as the [next section](../array-of-rows/) explains, how to write the literal for an array of such values.

This section uses the same approach as did the section that explained the rules for literal for an array of primitive values [here](../array-of-primitive-values/): first it states the rules; and then it illustrates these with examples.

## Statement of the rules

Just as in the section that stated the rules for literals for an array of primitive values, [here](../array-of-primitive-values/#statement-of-the-rules), the statement of these rules depends on understanding the notion of the canonical form of a literal.

If you follow the rules that are stated here, and illustrated in the demonstrations below, then you will always produce a syntactically valid literal which expresses the semantics that you intend. It turns out that very many other variants, especially for `text[]` values, are legal and can produce the result that you intend. However, we will not document the rules that govern these exotic uses because it is always sufficient to create your literals in canonical form.

Here is the sufficient set of rules.

- The commas that delimit successive values, and opening and closing parentheses, must not be surrounded by whitespace.
- Do _not_ surround the individual representations of numeric and `boolean` primitive values with double quotes.
- _Do_ surround the individual representations of  `timestamp` values with double quotes—even though this is not strictly necessary.
- _Do_ surround every individual representation of  a `text` value with double quotes, even though this is not always necessary. It _is_ necessary for any value that itself contains, as ordinary text, any whitespace or any of the characters that have syntactic significance within the outermost curly brace pair. This is the list:

```
   <space>   (   )   ,   "   \
```

- It's sufficient then simply to write all special characters ordinarily within the enclosing double quotes except for each of the double quote character itself and the backslash character. These must be escaped. The double quote character is escaped by doubling it up. And the backslash character is escaped with an immediately preceding single backslash.
- To specify that the value for a field is `null` , you must leave no whitespace between the pair of delimiters (Left parenthesis, comma, or right parenthesis) that surround its position. (This is the only choice.)

## Always write array literals in canonical form

Exactly the same considerations apply here as were explained in the same-named subsection in the section that explained the rules for literals for an array of primitive values, [here](../array-of-primitive-values/#always-write-array-literals-in-canonical-form).

## Examples to illustrate the rules

It will be sufficient to consider _"row"_ types with fields of just these data types: 

- numeric data types (like `int` and `numeric`);
- stringy data types (like `text`, `varchar`, and `char`);
- date-time data types (like `timestamp`);
- the `boolean` data type.

We'll use the _"row"_ type constructor to create representative values of each kind and inspect its `::text` typecast.

### "Row" type with `int` fields

This example demonstrates the principle:

```postgresql
create type row_t as (f1 int, f2 int, f3 int);
create table t(k serial primary key, v1 row_t, v2 row_t);
insert into t(v1) values (row(1, 2, 3)::row_t);
select v1::text as text_typecast from t where k = 1
\gset result_
\echo :result_text_typecast
```
The keyword `row` names the _"row"_ type constructor function. It is optional, but is used here for emphasis.

if you aren't already familiar with the `\gset` metacommand, you can read a brief account of how  it works in the section on `array_agg()` and `unnest()`, [here](../../functions-operators/array-agg-unnest). 

Notice that, in this example, the `select` statement is terminated by the `\gset` metacommand on the next line rather than by the usual semicolon. The `\gset` metacommand is silent. The `\echo` metacommand shows this:

```
(1,2,3)
```
In this case, the value of the `::text` typecast has the identical form to that of the _"row"_ type constructor. But, as we shall see below, this is not generally the case.

We can see the general form already:

- The (_value_ of) a _"row"_ type literal starts with the left parenthesis and ends with the right parenthesis.

- The items within the parentheses are delimited by commas, and there is no space between one  item, the comma, and the next item. Nor is there any space between the left parenthesis and the first item or between the last item and the right parenthesis.

The next subsection, _"Row type with text fields"_, shows that more needs to be said. But the two rules that we've already noticed always hold.

To use the literal that we produced to create a value, we must enquote it and typecast it. We can do this with the `\set` metacommand:
```postgresql
\set canonical_literal '\'':result_text_typecast'\''::row_t
\echo :canonical_literal
```
. The `\echo` metacommand now shows this:
```
'(1,2,3)'::row_t
```
Next we use the canonical literal that we have produced to update `t.v2` so that we can confirm that we have recreated the value that the row constructor created:
```postgresql
update t set v2 = :canonical_literal where k = 1;
select (v1 = v2)::text as "v1 = v2" from t where k = 1;
```
It shows this:
```
 v1 = v2 
---------
 true
```
As promised, the canonical form of the _"row"_ type literal does indeed recreate the identical value that the _"row"_ type constructor created.

### "Row" type with `text` fields

We'll use the _"Row type with int fields"_ example as a template for this and the subsequent subsections. The example sets array values each of which, apart from the single character `a`, needs some discussion. These are the characters (or, in one case, character sequence), listed here "bare" and with ten spaces between each:
```
     a       '       a b       ()       ,       "       \       null
```

```postgresql
create type row_t as (f1 text, f2 text, f3 text, f4 text, f5 text, f6 text, f7 text, f8 text);
create table t(k serial primary key, v1 row_t, v2 row_t);
insert into t(v1) values (
  ('a', $$'$$, 'a b', '()', ',', '"', '\', null)::row_t);

select v1::text as text_typecast from t where k = 1
\gset result_
\echo :result_text_typecast
```
Here, we omit the `row` keyword in the _"row"_ type constructor function to emphasize its optional status.

The `\echo` metacommand shows this:
```
(a,',"a b","()",",","""","\\",)
```
This is rather hard (for the human) to parse. To make the rules easier to see, we have surrounded the syntactically significant commas with three spaces on each side:
```
(   a   ,   '   ,   "a b"   ,   "()"   ,   ","   ,   """"   ,   "\\"   ,   )
```
**Note:** The introduction of spaces here, to help readability, is done _only_ for that purpose. Unlike is the case for an array literal, doing this actually affects the value that the literal produces. We shall demonstrate this at the end of this section.

In addition to the first two rules, we notice the following.

- Double quotes are used to surround a value that includes any spaces. (Though the example doesn't show it, this applies to leading and trailing spaces too.)
- The comma _has_ been surrounded by double quotes. This is because it _does_ have syntactic significance, as the value delimiter, within the parentheses of a _"row"_ type literal.
- The parentheses _have_ been surrounded by double quotes. This is because these _do_ have syntactic significance.
- The single quote is _not_ surrounded with double quotes. Though it has syntactic significance in other parsing contexts, it is insignificant within the parentheses of a _"row"_ type literal. This holds, also, for all sorts of other punctuation characters like `;` and  `:` and `[` and `]` and so on.
- The double quote has been escaped by doubling it up and this has been then surrounded with double quotes. This is because it _does_ have syntactic significance, as the (one and only) quoting mechanism, within the parentheses of a _"row"_ type literal.
- The backslash has also been escaped with another single backslash and this has been then surrounded with double quotes. This is because it _does_ have syntactic significance, as the escape character, within the parentheses of a _"row"_ type literal.
- `null` is represented in a _"row"_ type literal simply by the absence of any characters between two successive delimiters: between the left parenthesis and the first comma, between two successive commas, or between the last comma and the right parenthesis.

There's another rule that the present example does not show. Though not every comma-separated value was surrounded by double quotes, it's _never harmful_ to do this. You can confirm this easily with your own test, Yugabyte recommends that, for consistency, you always surround every `text` value within the parentheses of a _"row"_ type literal with double quotes.

To use the literal that we produced to create a value, we must enquote it and typecast it. We can do this, as we did for the `int` example above, with the `\set` metacommand. But we must use dollar quotes because the literal itself has an interior single quote.
```postgresql
\set canonical_literal '$$':result_text_typecast'$$'::row_t
\echo :canonical_literal
```
The `\echo` metacommand now shows this:
```
$$(a,',"a b","()",",","""","\\",)$$::row_t
```
Next we use the canonical literal that we have produced to update `t.v2` so that we can confirm that we have recreated the value that the row constructor created:
```postgresql
update t set v2 = :canonical_literal where k = 1;
select (v1 = v2)::text as "v1 = v2" from t where k = 1;
```
It shows this:
```
 v1 = v2 
---------
 true
```
So, again as promised, the canonical form of the array literal does indeed recreate the identical value that the _"row"_ type constructor created.

Finally in this subsection, we return to the meaning-changing effect of surrounding the comma delimiters with whitespace. Try this:
```postgresql
create type row_t as (f1 text, f2 text, f3 text);
select '(   a   ,   "(a b)"   ,   c   )'::row_t;;
```
It shows this:
```
 ("   a   ","   (a b)   ","   c   ")
```
You understand this by realizing that the entire run of characters between a pair of delimiters is taken as the value. And double quotes act as an _interior_ escaping mechanism. This model holds when, _but only when_, the value between a pair of delimiters is interpreted as a `text` value (because this is the datatype of the declared _"row"_ type field at this position).

This rule is different from the rule for an array literal. It's also different from the rules for JSON documents. In these cases, the value is entirely _within_ the double quotes, and whitespace around punctuation characters outside of the double-quoted values is insignificant.

**Note:** there is absolutely no need to take advantage of this understanding. Yugabyte recommends that you simply always use the "almost-canonical" form of the literal—in other words, you surround every single `text` value with double quotes, even when these are not needed, and you allow no whitespace between these double-quoted values and the delimiter at the start an end of each such value.

### "Row" type with `timestamp` fields

This example demonstrates the principle:

```postgresql
create type row_t as (f1 timestamp, f2 timestamp);
create table t(k serial primary key, v1 row_t, v2 row_t);
insert into t(v1) values (('2019-01-27 11:48:33', '2020-03-30 14:19:21')::row_t);
select v1::text as text_typecast from t where k = 1
\gset result_
\echo :result_text_typecast
```
The `\echo` metacommand shows this:

```
("2019-01-27 11:48:33","2020-03-30 14:19:21")
```
To use the literal that we produced to create a value, we must enquote it and typecast it. We can do this with the `\set` metacommand:
```postgresql
\set canonical_literal '\'':result_text_typecast'\''::row_t
\echo :canonical_literal
```
. The `\echo` metacommand now shows this:
```
'("2019-01-27 11:48:33","2020-03-30 14:19:21")'::row_t
```
Next we use the canonical literal that we have produced to update `t.v2` so that we can confirm that we have recreated the value that the row constructor created:
```postgresql
update t set v2 = :canonical_literal where k = 1;
select (v1 = v2)::text as "v1 = v2" from t where k = 1;
```
It shows this:
```
 v1 = v2 
---------
 true
```
Once again, as promised, the canonical form of the array literal does indeed recreate the identical value that the _"row"_ type constructor created.

### "Row" type with `boolean` fields

This example demonstrates the principle:

```postgresql
create type row_t as (f1 boolean, f2 boolean, f3 boolean);
create table t(k serial primary key, v1 row_t, v2 row_t);
insert into t(v1) values ((true, false, null)::row_t);
select v1::text as text_typecast from t where k = 1
\gset result_
\echo :result_text_typecast
```
The `\echo` metacommand shows this:
```
 (t,f,)
```
To use the literal that we produced to create a value, we must enquote it and typecast it. We can do this with the `\set` metacommand:
```postgresql
\set canonical_literal '\'':result_text_typecast'\''::row_t
\echo :canonical_literal
```
. The `\echo` metacommand now shows this:
```
'(t,f,)'::row_t
```
Next we use the canonical literal that we have produced to update `t.v2` so that we can confirm that we have recreated the value that the row constructor created:
```postgresql
update t set v2 = :canonical_literal where k = 1;
select (v1 = v2)::text as "v1 = v2" from t where k = 1;
```
It shows this:
```
 v1 = v2 
---------
 true
```
Yet again, as promised, the canonical form of the array literal does indeed recreate the identical value that the _"row"_ type constructor created.

## Further examples

There are other cases of interest like this:

- a _"row"_ type whose definition include one or more fields whose data types are other user-defined _"row"_ types.

The rules for such cases can be determined by induction from the rules that this section has stated and illustrated.

## "Row" type literal _versus_ "row" type constructor

The two notions, _type constructor_ and _literal_, are functionally critically different. It's easy to demonstrate the difference using a `DO` block, because this lets you use a declared variable. It's harder to do this using a SQL statement because you'd have to use a scalar subquery in place of the PL/pgSQL variable. The `row` keyword is deliberately omitted here to emphasize its optional status.

```postgresql
create type rt as (n numeric, s text, t timestamp, b boolean);

do $body$
declare
  n constant numeric := 42.17;
  s constant text := 'dog house';
  t constant timestamp := '2020-04-01 23:44:13';
  b constant boolean := true;
  r1 constant rt := (n, s, t, b)::rt;
  r2 constant rt := '(42.17,"dog house","2020-04-01 23:44:13",t)'::rt;
begin
  assert r1 = r2, 'unexpected';
end;
$body$;
```
You can use the _"row"_ type constructor an an expression in the `array[]` constructor (see [here](../../array-constructor)). But, of course, you can use only the literal for a _"row"_ type value within the the _literal_ for an array. The [next section](../array-of-rows/) explains this.
