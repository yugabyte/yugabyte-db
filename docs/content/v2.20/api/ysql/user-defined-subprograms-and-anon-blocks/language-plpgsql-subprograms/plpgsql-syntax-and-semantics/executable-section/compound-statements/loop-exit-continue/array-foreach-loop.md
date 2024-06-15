---
title: Array foreach loop [YSQL]
headerTitle: The "array foreach loop"
linkTitle: Array foreach loop
description: Describes the syntax and semantics of the PL/pgSQL's array foreach loop statements. [YSQL]
menu:
  v2.20:
    identifier: array-foreach-loop
    parent: loop-exit-continue
    weight: 30
type: docs
showRightNav: true
---

One-dimensional arrays are the most commonly used—and the use of even two-dimensional arrays is relatively rare. The use of arrays whose dimensionality is greater than two seems to be rarer still.

## "Array foreach loop" with a one-dimensional array

It's easy to define the semantics of the _array foreach loop_ for a one-dimensional array because the only semantically meaningful spelling of the _slice_ clause for such an array is _slice 0_.  (Notice that the argument of the _slice_ clause must be a non-negative _int_ literal; a negative _int_ literal causes the _42601_ syntax error.) Moreover the meaning when the optional _slice_ clause is omitted is defined to be the same as _slice 0_. Try this:

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;

create function s.f(slice_literal in int)
  returns table(z text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  arr constant text[] not null := array['red', 'green', 'blue'];
  element      text[] not null := '{}';
  n            int    not null := 0;
begin
  case slice_literal
    when 0 then
      <<"slice-0">>foreach z slice 0 in array arr loop
        return next;
      end loop "slice-0";
    when 1 then
      n := 0;
      <<"slice-1">>foreach element slice 1 in array arr loop
        n := n + 1;
      end loop "slice-1";
      assert (n = 1) and (element = arr);
      z := 'Performed just one iteration. The loop variable is set to the entire array.'; return next;
    when 2 then
      declare
        msg text not null := '';
      begin
        <<"slice-2">>foreach element slice 2 in array arr loop
          null;
        end loop "slice-2";
    exception when array_subscript_error then
      get stacked diagnostics msg := message_text;
      z := 'Caught: "'||msg||'".'; return next;
    end;
  end case;
end;
$body$;
```

Notice that he _loop variable_ for an _array foreach loop_ must be explicitly declared.

First invoke it to ask for _slice 0_:

```plpgsql
select s.f(0);
```

This is the result:

```output
 red
 green
 blue
```

This result is self-evidently useful.

Next invoke it to ask for _slice 1_:

```plpgsql
select s.f(1);
```

This is the result:

```output
 Performed just one iteration. The loop variable is set to the entire array.
```

This result shows that _slice 1_ with a one-dimensional array is pointless.

Finally invoke it to ask for _slice 2_:

```plpgsql
select s.f(2);
```

This is the result:

```output
 Caught: "slice dimension (2) is out of the valid range 0..1"
```

These three results, taken together, show that the only meaningful use of the _array foreach loop_ with a one-dimensional array is most compactly written by omitting the _slice_ clause, thus:

```plpgsql
create function s.f_slice_omitted()
  returns table(z text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  arr constant text[] not null := array['red', 'green', 'blue'];
begin
  <<"slice-0">>foreach z slice 0 in array arr loop
    return next;
  end loop "slice-0";
end;
$body$;

select s.f_slice_omitted();
```

The result is identical to that shown for _select s.f(0)_ shown above. It's easy to define the semantics:

- The _array foreach loop_ with a one-dimensional array is normally written without the _slice_ clause. The _loop variable_ is set, successively, to each element in the array in storage order.

## "Array foreach loop" with a multidimensional array

A useful demonstration of the semantics of the _slice_ clause requires a multidimensional array. Three dimensions are sufficient. Full generality is demonstrated by specifying values greater than one for each of the three lower bounds. Create the function _s.three_dim_array()_ to return such an array:

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;

create function s.three_dim_array(
    z_lb in int, z_ub in int,
    y_lb in int, y_ub in int,
    x_lb in int, x_ub in int)
  returns text[]
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  -- "z" is the major dimension
  z_len  constant int not null := z_ub - z_lb + 1;
  y_len  constant int not null := y_ub - y_lb + 1;
  x_len  constant int not null := x_ub - x_lb + 1;

  arr text[] not null :=
    array_fill(null::int, array[z_len, y_len, x_len], array[z_lb, y_lb, x_lb]);
begin
  assert array_lower(arr, 1) = z_lb;
  assert array_lower(arr, 2) = y_lb;
  assert array_lower(arr, 3) = x_lb;
  assert array_upper(arr, 1) = z_ub;
  assert array_upper(arr, 2) = y_ub;
  assert array_upper(arr, 3) = x_ub;

  -- Populate the array
  <<over_z>>for z in z_lb..z_ub loop
    <<over_y>>for y in y_lb..y_ub loop
      <<over_x>>for x in x_lb..x_ub loop
        arr[z][y][x] := 'z'||z::text||'-y'||y::text||'-x'||x::text;
      end loop over_x;
    end loop over_y;
  end loop over_z;

  return arr;
end;
$body$;
```

Notice how the function uses the _[array_fill()](../../../../../../../datatypes/type_array/functions-operators/array-fill/)_ SQL function to set up an empty array with the required bounds and whose elements all have the same, required, data type. And notice how it uses the _[array_lower](../../../../../../../datatypes/type_array/functions-operators/properties/#array-lower)_ and _[array_upper](../../../../../../../datatypes/type_array/functions-operators/properties/#array-upper)_ SQL functions to read the array's bounds.

- The first index, _z_, in the expression _arr\[z\]\[y\]\[x\]_ has bounds given by the expressions  _array_lower(arr, 1)_ and _array_upper(arr, 1)_.

- The second index, _y_, in the expression _arr\[z\]\[y\]\[x\]_ has bounds given by the expressions  _array_lower(arr, 2)_ and _array_upper(arr, 2)_.

- The third index, _x_,  in the expression _arr\[z\]\[y\]\[x\]_ has bounds given by the expressions  _array_lower(arr, 3)_ and _array_upper(arr, 3)_.

Before demonstrating the semantics of the _array foreach loop_'s _slice_ clause, it helps to look at the _text_ literal (i.e. the _text_ typecast) for the value that _s.three_dim_array()_ returns for a particular parameterization. Do this:

```plpgsql
select s.three_dim_array(
  z_lb=>11, z_ub=>12,
  y_lb=>23, y_ub=>25,
  x_lb=>36, x_ub=>39)::text;
```

{{< note title="Array literals." >}}
See: the section [Creating an array value using a literal](../../../../../../../datatypes/type_array/literals/); and the subsection [Array Input and Output Syntax](https://www.postgresql.org/docs/11/arrays.html#ARRAYS-IO) in the section [Arrays](https://www.postgresql.org/docs/11/arrays.html) in the PostgreSQL documentation.
{{< /note >}}

The result has no line breaks and so, in a screen window of usual width where line-wrap is turned on, it's very hard to read. Readability is enhanced by adding whitespace manually: line breaks; and a conventional indentation style to line up matching curly braces vertically.

The literal starts by representing the loop bounds, using square bracket pairs. The order is from the first dimension through the highest order dimension. Then an _=_ sign separates this from the list of element values.

The element values themselves are, by definition, listed in storage order. By definition, too, a run of values for the index that changes fastest, _x_, is surrounded by a curly-brace pair. Then, a run of such runs is surrounded by a second curly-brace pair when the index that changes next-fastest, _y_, changes. Finally, the entire list of element values is surrounded by a third curly-brace pair.

Use the massaged value, now, as the _text[]_ literal input actual for the _[unnest()](../../../../../../../datatypes/type_array/functions-operators/array-agg-unnest/#unnest)_ SQL function. This serves _both_ to ensure that the massage didn't introduce any typos _and_ to present the original array value in yet another way, thus:

```plpgsql
select unnest(
  '

    [11:12][23:25][36:39]=
      {
        {
          {z11-y23-x36,z11-y23-x37,z11-y23-x38,z11-y23-x39},
          {z11-y24-x36,z11-y24-x37,z11-y24-x38,z11-y24-x39},
          {z11-y25-x36,z11-y25-x37,z11-y25-x38,z11-y25-x39}
        },
        {
          {z12-y23-x36,z12-y23-x37,z12-y23-x38,z12-y23-x39},
          {z12-y24-x36,z12-y24-x37,z12-y24-x38,z12-y24-x39},
          {z12-y25-x36,z12-y25-x37,z12-y25-x38,z12-y25-x39}
        }
      }

  '::text[]
  );
```

This is the result:

```output
 z11-y23-x36
 z11-y23-x37
 z11-y23-x38
 z11-y23-x39
 z11-y24-x36
 z11-y24-x37
 z11-y24-x38
 z11-y24-x39
 z11-y25-x36
 z11-y25-x37
 z11-y25-x38
 z11-y25-x39
 z12-y23-x36
 z12-y23-x37
 z12-y23-x38
 z12-y23-x39
 z12-y24-x36
 z12-y24-x37
 z12-y24-x38
 z12-y24-x39
 z12-y25-x36
 z12-y25-x37
 z12-y25-x38
 z12-y25-x39
```

You can, of course, produce the same result directly like this:

```plpgsql
select unnest(s.three_dim_array(
                                 z_lb=>11, z_ub=>12,
                                 y_lb=>23, y_ub=>25,
                                 x_lb=>36, x_ub=>39)
                               );
```

The _unnest()_ function, by definition, creates a single-column table whose rows are the input array's element values in storage order.

Notice how the array's elements are addressed in the _s.three_dim_array()_ function:

```output
<<over_z>>for z in z_lb..z_ub loop
  <<over_y>>for y in y_lb..y_ub loop
    <<over_x>>for x in x_lb..x_ub loop
      arr[z][y][x] := 'z'||z::text||'-y'||y::text||'-x'||x::text;
    end loop over_x;
  end loop over_y;
end loop over_z;
```

You can picture each element, _arr\[z\]\[y\]\[x\]_, as a cube in a cuboid with the _x_-axis running from left to right, the _y_-axis running from bottom to top, and the _z_-axis running inwards from the _x-y_ origin—like the thumb, first finger, and index finger of your left hand, thus:

![imdb-erd](/images/api/ysql/plpgsql-loop-semantics/lh-coords.jpg)

You can use the exact same loop design to display the elements using a table function thus:

```output
<<over_z>>for z in z_lb..z_ub loop
  <<over_y>>for y in y_lb..y_ub loop
    t := '';
    <<over_x>>for x in x_lb..x_ub loop
      row_major_order_traversal := row_major_order_traversal||' '||arr[z][y][x];
      t := t||arr[z][y][x]||'  ';
    end loop over_x;
    return next;
  end loop over_y;
  t := ''; return next;
end loop over_z;
```

The complete table function, _s.without_slice_operator()_, is shown below. The innermost loop, _over_x_, concatenates the space-separated element values along the _x_-axis. The surrounding loop, _over_y_, displays each such concatenation of _x_ values on its own line. And the outermost loop, _over_z_, introduces a bank line between each set of _x_ values, one for each _y_ value, for a particular _z_ value, and the corresponding set for the next _z_ value. The nested loops produce this output:

```
 z11-y23-x36  z11-y23-x37  z11-y23-x38  z11-y23-x39
 z11-y24-x36  z11-y24-x37  z11-y24-x38  z11-y24-x39
 z11-y25-x36  z11-y25-x37  z11-y25-x38  z11-y25-x39

 z12-y23-x36  z12-y23-x37  z12-y23-x38  z12-y23-x39
 z12-y24-x36  z12-y24-x37  z12-y24-x38  z12-y24-x39
 z12-y25-x36  z12-y25-x37  z12-y25-x38  z12-y25-x39
```

This is the same order, the physical storage order, yet again.

This traversal order is called [_row-major order_](https://en.wikipedia.org/wiki/Row-_and_column-major_order). Notice that, for the _text_ literal, the elements are shown in exactly this order. By definition, it simply traverses the elements in order of their physical storage and accesses the associated metadata that specifies the array bounds:

- The elements along the _x_-axis are stored contiguously in order of increasing values of _x_.
- Each such _x_-axis run is stored contiguously with the next such run in order of increasing values of _y_.
- Each run of _x_ values over all the _y_ values (i.e. each _x-y_ plane) is stored contiguously with the next such plane in order of increasing values of _z_.

Here is the complete definition of the _s.without_slice_operator()_ function. Not only does it display the element values from the nested loop, (_over_x_ within _over_y_ within _over_z_); it also adds these sanity tests:

- It concatenates the space-separated element values from the nested loops traversal into the single text value, _row_major_order_traversal_.
- It strips off the array bounds representation from the start of the _text_ literal, _arr_to_text_typecast_; it removes all the curly braces; it removes all the commas; it reduces each run of spaces to just a single space; and it strips off any leading spaces.
- It concatenates the space-separated element values from the _unnest()_ result as the single _text_ value, _unnest_traversal_; and it strips off any leading spaces.
- It does a _foreach_ traversal of the array and concatenates the space-separated element values that this produces into a single text value _foreach_traversal_; and it strips off any leading spaces.
- Finally, it asserts that all of the _text_ values, _row_major_order_traversal_, _arr_to_text_typecast_, _unnest_traversal_, and _foreach_traversal_, are identical to each other.

Create and execute the table function, _s.without_slice_operator()_, thus:


```plpgsql
drop function if exists s.without_slice_operator() cascade;
create function s.without_slice_operator()
  returns table(t text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  --                                                   z       y       x
                                                    ------  ------  ------
  arr constant text[] not null := s.three_dim_array(11, 12, 23, 25, 36, 39);

  z_lb constant int not null := array_lower(arr, 1);
  z_ub constant int not null := array_upper(arr, 1);
  y_lb constant int not null := array_lower(arr, 2);
  y_ub constant int not null := array_upper(arr, 2);
  x_lb constant int not null := array_lower(arr, 3);
  x_ub constant int not null := array_upper(arr, 3);

  arr_to_text_typecast       text not null := arr::text;
  row_major_order_traversal  text not null := '';
  unnest_traversal           text not null := '';
  foreach_traversal          text not null := '';
  element                    text not null := '';
begin
  <<over_z>>for z in z_lb..z_ub loop
    <<over_y>>for y in y_lb..y_ub loop
      t := '';
      <<over_x>>for x in x_lb..x_ub loop
        row_major_order_traversal := row_major_order_traversal||' '||arr[z][y][x];
        t := t||arr[z][y][x]||'  ';
      end loop over_x;
      return next;
    end loop over_y;
    t := ''; return next;
  end loop over_z;

  declare
    old_len int not null := -1;

    -- The array litteral starts with the bounds like this: [11:12][23:25][36:39]=
    -- so need a regular expression to represent the general form of this.
    digits      constant text not null := '[0-9]+';
    subpattern  constant text not null := '\['||digits||':'||digits||'\]';
    pattern     constant text not null := subpattern||subpattern||subpattern||'=';
  begin
    arr_to_text_typecast := regexp_replace(arr_to_text_typecast, pattern, '');
    while (length(arr_to_text_typecast) <> old_len) loop
      arr_to_text_typecast := replace(arr_to_text_typecast, '{',  '');
      arr_to_text_typecast := replace(arr_to_text_typecast, '}',  '');
      arr_to_text_typecast := replace(arr_to_text_typecast, ',',  ' ');
      arr_to_text_typecast := replace(arr_to_text_typecast, '  ', ' ');
      old_len := length(arr_to_text_typecast);
    end loop;
  end;

  for element in (select unnest(arr)) loop
    unnest_traversal := unnest_traversal||' '||element;
  end loop;

  foreach element in array arr loop
    foreach_traversal := foreach_traversal||' '||element;
  end loop;

  row_major_order_traversal := ltrim(row_major_order_traversal);
  arr_to_text_typecast      := ltrim(arr_to_text_typecast);
  unnest_traversal          := ltrim(unnest_traversal);
  foreach_traversal         := ltrim(foreach_traversal);

  assert arr_to_text_typecast = row_major_order_traversal;
  assert unnest_traversal     = row_major_order_traversal;
  assert foreach_traversal    = row_major_order_traversal;
end;
$body$;

select s.without_slice_operator();
```

The result has already been shown above.

## Summary

- The _array foreach loop_, when the argument is an array, _arr_, of any dimensionality and when the _slice_ clause is omitted (or when the _slice 0_ clause is used):
  ```plpgsql
  foreach element in array arr loop
    ...
  end loop;
  ```
  simply produces each successive array element, with each successive iteration, in physical storage order—_a.k.a._ row-major order.

- This _query for loop_ has exactly the same effect:
  ```plpgsql
  for element in (select unnest(arr)) loop
    ...
  end loop;
  ```
- The _text_ typecast of an array, _arr::text_, also lists the element values in physical storage order.
- Using a three dimensional array as an example, an explicit traversal that uses an _over_x_ loop inside an _over_y_ loop inside an _over_z_ loop. where the elements are addressed using _arr\[z\]\[y\]\[x\]_, also lists the element values in physical storage order.
