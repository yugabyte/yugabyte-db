---
title: array_agg() and unnest()
linkTitle: array_agg() / unnest()
headerTitle: array_agg() and unnest()
description: array_agg() and unnest()
menu:
  latest:
    identifier: array-agg-unnest
    parent: array-functions-operators
isTocNested: false
showAsideToc: false
---

For one-dimensional arrays, but _only for these_ (see [Multidimensional `array_agg()` and `unnest()`](./#multidimensional-array-and-and-unnest)), these two functions have mutually complementary effects in the following sense. After this sequence (the notation is informal):
```
array_agg of "SETOF tuples #1" => "result array"
unnest of "result array" => "SETOF tuples #3"
```
The `SETOF tuples #3` has identical shape and content to that of `SETOF tuples #1`. And the data type of `result array` is an array of the data type of the tuples.

For this reason, the two functions, `array_agg()` and `unnest()`, are described in the same section.

## array_agg()

In normal use, `array_agg()` is applied to the _select list_ from a physical table, or maybe from a view that encapsulates the query. This is shown in the _"[Realistic use case](./#realistic-use-case)"_ example below. But first, you can demonstrate the functionality without creating and populating a table by using, instead, the `values` statement. Try this:

```postgresql
values
  (1::int, 'dog'::text),
  (2::int, 'cat'::text),
  (3::int, 'ant'::text);
```

It produces this result:
```
 column1 | column2 
---------+---------
       1 | dog
       2 | cat
       3 | ant
```
Notice that YSQL has named the _select list_ items `column1` and `column2`. The result is a so-called `SETOF`. It means a set of rows, just as is produced by a `SELECT` statement. (You'll see this word if you describe the `generate_series()` built-in table function with the `\df` metacommand.) To use the rows that the `values` statement produces as the input for `array_agg()`, you need to use a named `type`, thus:
```postgresql
create type rt as (k int, v text);

with tab as (
  values
    (1::int, 'dog'::text),
    (2::int, 'cat'::text),
    (3::int, 'ant'::text))
select array_agg((column1, column2)::rt order by column1) as array_literal
from tab;
```
It produces this result:
```
          array_literal          
---------------------------------
 {"(1,dog)","(2,cat)","(3,ant)"}
```
You recognize this as the text of the literal that represents an array of tuples that are shape-compatible with type rt. Recall from the [`array[]` constructor](../../array-constructor/) section that this value doesn't encode the type name. In fact, you could typecast it to any shape compatible type.

You can understand the effect of `array_agg()` thus:

- Treat each row as a `rt[]` array with a single-value.
- Concatenate (see the [`||` operator](../concatenation/#the-operator)) the values from all the rows in the specified order into a new `rt[]` array.

This code illustrates this point:
```postgresql
-- Eqivalent to this:
with tab as (
  values
    ((1, 'dog')::rt),
    ((2, 'cat')::rt),
    ((3, 'ant')::rt)
  )
select array_agg(column1 order by column1) as array_literal
from tab;

-- Can be seen as this:
with tab as (
  values
    ((1, 'dog')::rt),
    ((2, 'cat')::rt),
    ((3, 'ant')::rt)
  )
select
  array[(1, 'dog')::rt] ||
  array[(2, 'cat')::rt] ||
  array[(3, 'ant')::rt]
as arr;
```
To prepare for the demonstration of `unnest()`, save this single-valued result into a `ysqlsh` variable by using the `\gset` metacommand. This takes a single argument, conventionally spelled with a trailing underscore (for example, `result_`) and re-runs the `SELECT` statement that, as the last submitted `ysqlsh` command, is still in the command buffer. (If the `SELECT` doesn't return a single row, then you get a clear error.) In general, when the _select list_ has _N_ members, called `c1` through `cN`, each of these values is stored in automatically-created variables called `result_c1` through `result_cN`.

if you aren't already familiar with the `\gset` metacommand, you can read a brief account of how it works in the section [Meta-commands](../../../../../../admin/ysqlsh/#meta-commands) within the major section on `ysqlsh`.

Immediately after running the `with... select array_agg(...) as array_literal...` query above, do this:

```postgresql
\gset result_
\echo :result_array_literal
```
The `\gset` metacommand is silent. The `\echo` metacommand shows this:
```
{"(1,dog)","(2,cat)","(3,ant)"}
```
The text of the literal is now available for re-use, as was intended.
## unnest()

As the sketch at the start of this page indicated, the input to unnest is an array. To use what the code example in the account of `array_agg()` set in the `ysqlsh` variable `result_array_literal` in a SQL statement, you must quote it and typecast it to `rt[]`. This is easily done with the `\set` metacommand, thus:

```postgresql
\set unnest_arg '\'':result_array_literal'\'::rt[]'
\echo :unnest_arg
```

The `\set` metacommand uses the backslash character to escape the single quote character that it also uses to surround the string that it assigns to the target `ysqlsh` variable. The `\echo` metacommand shows this:
```
'{"(1,dog)","(2,cat)","(3,ant)"}'::rt[]
```
Now use it as the actual argument for `unnest()` thus:
```postgresql
with
  rows as (
    select unnest(:unnest_arg) as rec)
select
  (rec).k,
  (rec).v
from rows
order by 1;
```
The parentheses around the column alias `rec` are required to remove what the SQL compiler would otherwise see as an ambiguity, and would report as a _"42P01 undefined_table"_ error. This is the result:

```
 k |  v  
---+-----
 1 | dog
 2 | cat
 3 | ant
```

As promised, the original `SETOF` tuples has been recovered.

## Multidimensional array_agg() and unnest()

Start by aggregating three `int[]` array instances and by preparing the result as an `int[]` literal for the next step using the same `\gset` technique that was used above:
```postgresql
with tab as (
  values
    ('{1, 2, 3}'::int[]),
    ('{4, 5, 6}'::int[]),
    ('{7, 8, 9}'::int[]))
select array_agg(column1 order by column1) as array_literal
from tab

\gset result_
\set unnest_arg '\'':result_array_literal'\'::int[]'
\echo :unnest_arg
```
Notice that the SQL statement, this time, is _not_ terminated with a semicolon. Rather, the `\gset` metacommand acts as the terminator. This simply makes the `ysqlsh` output less noisy. This is the result:

```
'{{1,2,3},{4,5,6},{7,8,9}}'::int[]
```
You recognize this as the literal for a two-dimensional array. Now use this as the actual argument for `unnest()`:
```postgresql
select unnest(:unnest_arg) as val 
order by 1;
```
It produces this result:
```
 val 
-----
   1
   2
   3
   4
   5
   6
   7
   8
   9
```
This `SETOF` result lists all of the input array's "leaf" values in row-major order. This term is explained in the [Joint semantics](./functions-operators/properties/#joint-semantics)) section within the _"Functions for reporting the geometric properties of an array"_ section.

Notice that, for the multidimensional case, the original input to `array_agg()` was _not_, therefore, regained. This point is emphasized by aggregating the result:

```postgresql
with v as
  (select unnest(:unnest_arg) as val)
select array_agg(val order by val) from v;
```
It produces this result:
```
      array_agg      
---------------------
 {1,2,3,4,5,6,7,8,9}
```
You started with a two-dimensional array. But now you have a one-dimensional array with the same values as the input array in the same row-major order.

This result has the same semantic content that the `array_to_string()` function produces:

```postgresql
select array_to_string(:unnest_arg, ',');
```
It produces this result:
```
  array_to_string  
-------------------
 1,2,3,4,5,6,7,8,9
```

## Realistic use case

The basic illustration of the functionality of `array_agg()` showed how it can convert the entire contents of a table (or, by extension, the `SETOF` rows defined by a `SELECT` execution) into a single array value. This can be useful to return a large `SELECT` result in its entirety (in other words, in a single round trip) to a client program.

Another use is to populate a single newly-created _"masters_with_details"_ table from the fully projected and unrestricted _inner join_ of a classic _"masters"_ and _"details"_ pair of tables. The new table has all the columns that the source _"masters"_ table has and all of its rows. And it has an additional _"details"_ column that holds, for each _"masters"_ row, a _"details_t[]"_ array that represents all of the child rows that it has in the source _"details"_ table. The type _"details_t"_ has all of the columns of the _"details"_ table except the _"details.masters_pk"_ foreign key column. This column vanishes because, as the _join_ column, it vanishes in the _"inner join"_. The _"details"_ table's "payload" is now held in place in a single multivalued field in the new _"masters_with_details"_ table.

Start by creating and populating the _"masters"_ and _"details"_ tables:
```postgresql
create table masters(
  master_pk int primary key,
  master_name text not null);

insert into masters(master_pk, master_name)
values
  (1, 'John'),
  (2, 'Mary'),
  (3, 'Joze');

create table details(
  master_pk int not null,
  seq int not null,
  detail_name text not null,

  constraint details_pk primary key(master_pk, seq),

  constraint master_pk_fk foreign key(master_pk)
    references masters(master_pk)
    match full
    on delete cascade
    on update restrict);

insert into details(master_pk, seq, detail_name)
values
  (1, 1, 'cat'),    (1, 2, 'dog'),
  (2, 1, 'rabbit'), (2, 2, 'hare'), (2, 3, 'squirrel'), (2, 4, 'horse'),
  (3, 1, 'swan'),   (3, 2, 'duck'), (3, 3, 'turkey');
```
Next, create a view that encodes the fully projected, unrestricted _inner join_ of the original data, and inspect the result set that it represents:
```postgresql
create view original_data as
select
  master_pk,
  m.master_name,
  d.seq,
  d.detail_name
from masters m inner join details d using (master_pk);

select
  master_pk,
  master_name,
  seq,
  detail_name
from original_data
order by
master_pk, seq;
```
This is the result:
```
 master_pk | master_name | seq | detail_name 
-----------+-------------+-----+-------------
         1 | John        |   1 | cat
         1 | John        |   2 | dog
         2 | Mary        |   1 | rabbit
         2 | Mary        |   2 | hare
         2 | Mary        |   3 | squirrel
         2 | Mary        |   4 | horse
         3 | Joze        |   1 | swan
         3 | Joze        |   2 | duck
         3 | Joze        |   3 | turkey
```
Next, create the type `details_t` and the new table:

```postgresql
create type details_t as (seq int, detail_name text);

create table masters_with_details (
  master_pk int primary key,
  master_name text not null,
  details details_t[] not null);
```
Notice that you made the _"details"_ column `not null`. This was a choice. It adds semantics that are very hard to capture in the original two table design without tricky, and therefore error-prone, programming of triggers and the like. You have implemented the so-called _"mandatory one-to-many"_ rule. In the present example, the rule says (in the domain of the entity-relationship model that specifies the requirements) that an occurrence of a _"Master"_ entity type cannot exist unless it has at least one, but possibly many, child occurrences of a _"Detail"_ entity type.

Next, populate the new table and inspect its contents:
```postgresql
insert into masters_with_details
select
  master_pk,
  master_name,
  array_agg((seq, detail_name)::details_t order by seq) as agg
from original_data
group by master_pk, master_name;

select master_pk, master_name, details
from masters_with_details
order by 1;
```
This is the result:
```
 master_pk | master_name |                       details                        
-----------+-------------+------------------------------------------------------
         1 | John        | {"(1,cat)","(2,dog)"}
         2 | Mary        | {"(1,rabbit)","(2,hare)","(3,squirrel)","(4,horse)"}
         3 | Joze        | {"(1,swan)","(2,duck)","(3,turkey)"}
```
Here's a helper function to show the primitive values that the `details_t[]` array encodes without the clutter of the array literal syntax:

```postgresql
create function pretty_details(arr in details_t[])
  returns text
  immutable
  language plpgsql
as $body$
declare
  arr_type constant text := pg_typeof(arr);
  ndims constant int := array_ndims(arr);
  lb constant int := array_lower(arr, 1);
  ub constant int := array_upper(arr, 1);
begin
  assert arr_type = 'details_t[]', 'assert failed: ndims = %', arr_type;
  assert ndims = 1, 'assert failed: ndims = %', ndims;
  declare
    line text not null :=
      rpad(arr[lb].seq::text||': '||arr[lb].detail_name::text, 12)||
      ' | ';
  begin
    for j in (lb + 1)..ub loop
      line := line||
      rpad(arr[j].seq::text||': '||arr[j].detail_name::text, 12)||
      ' | ';
    end loop;
    return line;
  end;
end;
$body$;
```
Notice that this is not a general purpose function. Rather, it expects that the input is a `details_t` array. So it first checks that this pre-condition is met. It then discovers the lower and upper bounds of the array so that it can loop over its values. It uses these functions for reporting the geometric properties of the input array: [`array_ndims()`](../../functions-operators/properties/#array-ndims); [`array_lower()`](../../functions-operators/properties/#array-lower); and [`array_upper()`](../../functions-operators/properties/#array-upper).

Invoke it like this:

```postgresql
select master_pk, master_name, pretty_details(details)
from masters_with_details
order by 1;
```
It produces this result:
```
 master_pk | master_name |                        pretty_details                        
-----------+-------------+--------------------------------------------------------------
         1 | John        | 1: cat       | 2: dog       | 
         2 | Mary        | 1: rabbit    | 2: hare      | 3: squirrel  | 4: horse     | 
         3 | Joze        | 1: swan      | 2: duck      | 3: turkey    |
```
Next, create a view that uses `unnest()` to re-create the effect of the fully projected, unrestricted _inner join_ of the original data, and inspect the result set that it represents:
```postgresql
create view new_data as
with v as (
  select
    master_pk,
    master_name,
    unnest(details) as details
  from masters_with_details)
select
  master_pk,
  master_name,
  (details).seq,
  (details).detail_name
from v;

select
  master_pk,
  master_name,
  seq,
  detail_name
from new_data
order by
master_pk, seq;
```
The result is identical to what the _"original_data"_ view represents. But rather than relying on visual inspection, can check that the _"new_data"_ view and the the _"original_data"_ view represent the identical result by using SQL thus:
```postgresql
with
  original_except_new as (
    select master_pk, master_name, seq, detail_name 
    from original_data
    except
    select master_pk, master_name, seq, detail_name
    from new_data),

  new_except_original as (
    select master_pk, master_name, seq, detail_name
    from new_data
    except
    select master_pk, master_name, seq, detail_name
    from original_data),

  original_except_new_union_new_except_original as (
    select master_pk, master_name, seq, detail_name
    from original_except_new
    union
    select master_pk, master_name, seq, detail_name
    from new_except_original)

select
  case count(*)
    when 0 then '"new_data" is identical to "original_data."'
    else        '"new_data" differs from "original_data".'
  end as result
from original_except_new_union_new_except_original;
```
This is the result:
```
                   result                    
---------------------------------------------
 "new_data" is identical to "original_data."
```
Notice that if you choose the _"masters_with_details"_ approach (either as a migration from a two-table approach in an extant application, or as an initial choice in a new application) you must appreciate the trade-offs.

**Prerequisite:**

- You must be confident that the _"details"_ rows are genuinely private each to its own master and do not implement a many-to-many relationship in the way that the _"order_items"_ table does between the _"customers"_ table and the _"items"_ table in the classic sales order entry model that is frequently used to teach table design according to the relational model.

**Pros:**

- You can enforce the mandatory one-to-many requirement declaratively and effortlessly.
- Changing and querying the data will be faster because you use single table, single-row access rather than two-table, multi-row access.
- You can trivially recapture the query functionality of the two-table approach by implementing a _"new_data"_ unnesting view as has been shown. So you can still find, for example, rows in the _"masters_with_details"_ table where the "details" array has the specified values like this:
```postgresql
with v as (
  select master_pk, master_name, seq, detail_name
  from new_data
  where detail_name in ('rabbit', 'horse', 'duck', 'turkey'))
select
  master_pk,
  master_name,
  array_agg((seq, detail_name)::details_t order by seq) as agg
from v
group by master_pk, master_name
order by 1;
```
&#160;&#160;&#160;&#160;This is the result:
```
 master_pk | master_name |            agg             
-----------+-------------+----------------------------
         2 | Mary        | {"(1,rabbit)","(4,horse)"}
         3 | Joze        | {"(2,duck)","(3,turkey)"}
```
**Cons:**
- Changing the data in the "details" array is rather difficult. Try this (in the two-table regime):
```postgresql
update details
set detail_name = 'bobcat'
where master_pk = 2
and detail_name = 'squirrel';

select
  master_pk,
  master_name,
  seq,
  detail_name
from original_data
where master_pk = 2
order by
master_pk, seq;
```
&#160;&#160;&#160;&#160;This is the result:
```
 master_pk | master_name | seq | detail_name 
-----------+-------------+-----+-------------
         2 | Mary        |   1 | rabbit
         2 | Mary        |   2 | hare
         2 | Mary        |   3 | bobcat
         2 | Mary        |   4 | horse
```
- Here's how you achieve the same effect, and check that it worked as intended, in the new regime. Notice that you need to know the value of _"seq"_ for the _"rt"_ object that has the _"detail_name"_ value of interest. This is easy to do by implementing a dedicated PL/pgSQL function that encapsulates `array_replace()` or that replaces a value directly by addressing it using its index. But it's hard to do without that. (These methods are described in [`array_replace()` and setting an array value explicitly](../../functions-operators/replace-a-value/).)

```postgresql
with v as (
  select array_replace(details, '(3,squirrel)', '(3,bobcat)')
  as new_arr from masters_with_details where master_pk = 2)
update masters_with_details
set details = (select new_arr from v)
where master_pk = 2;

select
  master_pk,
  master_name,
  seq,
  detail_name
from new_data
where master_pk = 2
order by
master_pk, seq;
```
&#160;&#160;&#160;&#160;The result is identical to the result shown for querying _"original_data"_ above.

**Note:** The `UPDATE` statement, using as it does a subquery from a view defined in a `WITH` clause as the actual argument for `array_replace()`, seems to be unnecessarily complex. You might expect to use this:

```
update masters_with_details
set details = array_replace(details, '(3,squirrel)', '(3,bobcat)')
where master_pk = 2;
```

The more complex, but semantically equivalent, locution is used as a workaround for [GitHub Issue #4296](https://github.com/yugabyte/yugabyte-db/issues/4296). For more information, see [`array_replace()`](../replace-a-value/#array-replace). The code above will be updated when that issue is fixed.

- Implementing the requirement that the values of _"detail_name"_ must be unique for a given _"masters"_ row is trivial in the old regime:
```postgresql
create unique index on details(master_pk, detail_name);
```
To achieve the effect in the new regime, you'd need to write a PL/pgSQL function, with return type `boolean` that scans the values in the _"details"_ array and returns `true` when there are no duplicates among the values of the _"detail_name"_ field and that otherwise returns `false`. Then you'd use this function as the basis for a check constraint in the definition of the _"details_with_masters"_ table. This is a straightforward programming task, but it does take more effort than the simple declarative implementation of the business rule that the two-table regime allows.
