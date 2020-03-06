---
title: Concatenation (`||`)
linktitle: Concatenation (`||`)
summary: Concatenation: the `||` operator
description: Concatenation: the `||` operator
menu:
  latest:
    identifier: to-jsonb
    parent: functions-operators
isTocNested: true
showAsideToc: true
---





## _jsonb_array_elements_text()_ and _json_array_elements_text()_

Here is the signature for the `jsonb` variant:

```
input value        jsonb
return value       SETOF text
```

The function `jsonb_array_elements_text()` bears the same relationship to `jsonb_array_elements()` that the other `*text()` functions bear to their plain counterparts: it's the same relationship that the `->>` and `#>>` operators bear, respectively to `->` and `#>`. This example uses the same JSON _array_ input that was used to illustrate `jsonb_array_elements()` .

```postgresql
do $body$
declare
  j_array constant jsonb := '["cat", "dog house", 42, true, {"x": 17}, null]';
  t text;

  elements text[];
  expected_elements constant text[] :=
    array[
      'cat',
      'dog house',
      '42',
      'true',
      '{"x": 17}',
      null
    ];

  n int := 0;
begin
  for t in (select * from jsonb_array_elements_text(j_array)) loop
    n := n + 1;
    elements[n] := t;
  end loop;

  for n in 1..5 loop
    assert
      elements[n] = expected_elements[n],
    'unexpected';
  end loop;

  assert
    elements[6] is null and expected_elements[6] is null,
  'unexpected';
end;
$body$;
```

This highlights the fact that the resulting values are equivalent SQL primitive values, cast to `text` values, rather than the RFC 7159 representations of the actual JSON values. In particular, `42` is the two characters `4` and `2` and `true` is the four characters `t` , `r`, `u`, and `e`.  Moreover, the sixth row has a genuine `null`. This is why the `assert` needs to be programmed more verbosely than for the `jsonb_array_elements()` example.

This example emphasizes the impedance mismatch between a JSON _array_ and a SQL `array`: the former allows values of heterogeneous data types; but the latter allows only values of the same data type—as was used to declare the array.

If you have prior knowledge of the convention to which the input JSON document adheres, you can cast the output of `jsonb_array_elements_text()` to, say, `integer` or `boolean`. For example, this:

```postgresql
create domain length as numeric
  not null
  check (value > 0);

select value::length
from jsonb_array_elements_text(
  '[17, 19, 42, 47, 53]'::jsonb
  );
```

generates a table of genuine `integer` values that honor the constraints that the `domain` defines. If you make one of the input elements negative, then you get this error:

```
value for domain length violates check constraint "length_check"
```

And if you make of the input elements the JSON `null`, then you get this error:

```
domain length does not allow null values
```

Here's the same idea for `boolean` values:

```postgresql
create domain truth as boolean
  not null;

select value::truth
from jsonb_array_elements_text(
  '[true, false, true, false, false]'::jsonb
  );
```

It produces this output in _ysqlsh_:

```
 value
-------
 t
 f
 t
 f
 f
```

Notice that `t` and `f` are the _ysqlsh_ convention for displaying `boolean` values.
