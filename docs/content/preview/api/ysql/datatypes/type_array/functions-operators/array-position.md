---
title: array_position() and array_positions()
linkTitle: array_position(), array_positions()
headerTitle: array_position() and array_positions()
description: array_position() and array_positions()
menu:
  preview:
    identifier: array-position
    parent: array-functions-operators
type: docs
---
These functions require that the to-be-searched array is one-dimensional. They return the index values of the specified to-be-searched-for value in the specified to-be-searched array.

Create a function to return the test array:

```plpgsql
drop function if exists arr() cascade;
create function arr()
  returns text[]
  language sql
as $body$
  select array[
                'mon', 'tue', 'wed', 'thu','fri', 'sat', 'sun', 
                'mon', 'tue', 'wed', 'thu','fri', 'sat', 'sun',
                'mon', 'tue', 'wed'
              ];
$body$;
```

List the elements of the _days_arr_ column (in the only row that the view produces) by their index number. Use the built-in function _[generate_subscripts()](../array-agg-unnest/#generate-subscripts)_.

```plpgsql
with c(days, pos) as (
  select a, subscripts.pos
  from arr() as a
  cross join lateral
  generate_subscripts(arr(), 1) as subscripts(pos))
select pos, days[pos] as day from c order by pos;
```

This is the result:

```output
 pos | day 
-----+-----
   1 | mon
   2 | tue
   3 | wed
   4 | thu
   5 | fri
   6 | sat
   7 | sun
   8 | mon
   9 | tue
  10 | wed
  11 | thu
  12 | fri
  13 | sat
  14 | sun
  15 | mon
  16 | tue
  17 | wed
```

The examples below use the _arr()_ function value.

## array_position()

**Purpose:** Return the index, in the supplied array, of the specified value. Optionally, starts searching at the specified index.

**Signature:**

```output
input value:       anyarray, anyelement [, int]
return value:      int
```
**Note:** The optional third parameter specifies the _inclusive_ index value at which to start the search.

**Example:**

```plpgsql
select array_position(
    arr(),        -- The to-be-searched array.
    'tue'::text,  -- The to-be-searched-for value.
    3::int        -- The (inclusive) position
                  --   at which to start searching. [optional]
  ) as position;
```

This is the result:

```output
 position 
----------
        9
```

## array_positions()

**Purpose:** Return the indexes, in the supplied array, of all occurrences of the specified value.

**Signature:**

```output
input value:       anyarray, anyelement
return value:      integer[]
```
**Example:**

This example uses the _[unnest()](../array-agg-unnest/#unnest)_ built-in function to present the elements of the array that _array_positions()_ returns as a table:

```plpgsql
select unnest(
  array_positions(
      arr(),       -- The to-be-searched array.
      'tue'::text  -- The to-be-searched-for value.
    )
  ) as position;
```

This is the result:

```output
 position 
----------
        2
        9
       16
```
