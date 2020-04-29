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

> **Monday 27-Apr-2020. To_Do. This section is still work-in-progress. The text and example needed to complete it are ready in draft. The work to incorporate this is relatively small.**

**Note: What the PostgreSQL documentation says about _"row"_ types and _"record"_ types.**

- From the section [43.3.4. Row Types](https://www.postgresql.org/docs/11/plpgsql-declarations.html#PLPGSQL-DECLARATION-ROWTYPES): _A variable of a composite type is called a "row" variable (or "row" type variable)._

- The word "row" therefore has two different uses—but these uses are really the different sides of the same coin. A row in a schema-level table is actually an occurrence of a _"row"_ type—in other words, a _"row"_ type value. In this case, the schema-level _"row"_ type is created automatically as a side effect of executing the _"create table"_ statement. It has the same name as the table. (This is allowed because tables and types are in different namespaces.) Further, a column in a schema-level table can have a user-defined _"row"_ type as its data type, and in this case the _"row"_ type need not be associated with a table.

- From the section [43.3.5. Record Types](https://www.postgresql.org/docs/11/plpgsql-declarations.html#PLPGSQL-DECLARATION-RECORDS): _"Record" variables are similar to "row" type variables, but they have no predefined structure... The substructure of a "record" variable can change each time it is assigned... Note that "record" is not a true data type, only a placeholder._

- So a _"record"_ type is defined only implicitly and is anonymous.

We need first to understand how to write a literal for a _"row"_ type value before we can understand how to write the literal for an array of such values. We'll use the term _"stringy"_ to denote the set of character data types: `text`, `varchar`, and `char`. 

Create this _"row"_ type:

```postgresql
create type rt as (n numeric, b boolean, t timestamp);
```
and this table:
```postgresql
create table t(k serial primary key, r rt);
```
Now insert a table row using the type constructor:
```postgresql
insert into t(r) values
  (row(42.17::numeric, true::boolean, '2020-04-01 23:44:13'::timestamp)::rt);
```
The keyword `row` is the _"row"_ type constructor function. It is optional, but is used here for emphasis.

And now insert a table row using the literal for that type:

```postgresql
insert into t(r) values
  ('(53.71, false, 2020-02-07 12:27:09)'::rt);
```
Notice that the `timestamp` literal, in standard directly typecastable format, has an interior space separating the date component and the time component. It's usual to surround this with double quotes to improve readability. As (see below), the `::text` typecast of the row value uses them. But they are optional. The punctuation marks `,` and `)` are sufficient to establish the string of characters as a single value.

The two notions, _type constructor_ and _literal_, are critically different. It's easy to demonstrate the difference using a `DO` block, because this lets you use a declared variable It's harder to do this using a SQL statement because you'd have to use a scalar subquery in place of the PL/pgSQL variable. The `row` keyword is deliberately omitted here to emphasize its optional status.

```postgresql
do $body$
declare
  n constant numeric := 42.17;
  b constant boolean := true;
  d constant timestamp := '2020-04-01 23:44:13';
  r1 constant rt := (n, b, d)::rt;
  r2 constant rt := '(42.17, true, 2020-04-01 23:44:13)'::rt;
begin
  assert r1 = r2, 'unexpected';
end;
$body$;
```
Moreover, To_Do as the section _[The literal for an array of values of a _"row"_ type with `int` fields](../To_Do/)_ shows, you must, of course, use a _literal_ for a _"row"_ type inside the _literal_ for an array.

Check the result of the two `insert` statements in the obvious way:

```postgresql
select k, r::text from t order by k;
```
It produces this result:
```
 k |                r                
---+---------------------------------
 1 | (42.17,t,"2020-04-01 23:44:13")
 2 | (53.71,f,"2020-02-07 12:27:09")
```
This looks a lot like the literal values used for the input. But notice that the whitespace in the input (outside of the timestamp value itself) was insignificant and the convention for the `::text` type cast of a row value is to remove the whitespace between values.

The best way to confirm that the `insert` really did produce the intended result is to specify the row fields individually, thus:

```postgresql
select k, (r).n::text, (r).b::text, (r).t::text from t order by k;
```
(The parentheses around `r` are essential.) This is the result:
```
 k |   n   |   b   |          t          
---+-------+-------+---------------------
 1 | 42.17 | true  | 2020-04-01 23:44:13
 2 | 53.71 | false | 2020-02-07 12:27:09
```
