---
title: Operators for constructing jsonb value
linkTitle: Operators for constructing jsonb value
summary: Operators for constructing jsonb value
description: Operators for constructing jsonb value
menu:
  latest:
    identifier: jsonb-operators-constructing-values
    parent: functions-operators
    weight: 170
isTocNested: true
showAsideToc: true
---

These operators require that the inputs are presented as `jsonb` values. They don't have overloads for `json`.

## Remove single key-value pair from an _object_ or a single value from an _array_: the `-` operator

Notice that, for all the operators whose behavior is described by using the term "remove", this is a convenient shorthand. The actual effect of these operators is to create a _new_ `jsonb` value from the `jsonb` value on the left of the operator according to the rule that the operator implements, parameterized by the SQL value on the right of the operator.

### Remove key-value pair(s) from an _object_

To remove a single key-value pair:

```postgressql
do $body$
declare
  j_left constant jsonb := '{"a": "x", "b": "y"}';
  key constant text := 'a';
  j_expected constant jsonb := '{"b": "y"}';
begin
  assert
    j_left - key = j_expected,
 'assert failed';
end;
$body$;
```

To remove several key-value pairs:

```postgresql
do $body$
declare
  j_left constant jsonb := '{"a": "p", "b": "q", "c": "r"}';
  key_list constant text[] := array['a', 'c'];
  j_expected constant jsonb := '{"b": "q"}';
begin
  assert
    j_left - key_list = j_expected,
 'assert failed';
end;
$body$;
```

### Remove single value from an _array_

Thus:

```
do $body$
declare
  j_left constant jsonb := '[1, 2, 3, 4]';
  idx constant int := 0;
  j_expected constant jsonb := '[2, 3, 4]';
begin
  assert
    j_left - idx = j_expected,
 'assert failed';
end;
$body$;
```

There seems not to be a way to remove several values from an _array_ at a list of indexes analogous to the ability to remove several key-value pairs from an _object_ with a list of pair keys. The obvious attempt fails with this error:

```
operator does not exist: jsonb - integer[]
```

Obviously, you can achieve the result at the cost of verbosity thus:

```postgresql
do $body$
declare
  j_left constant jsonb := '[1, 2, 3, 4, 5, 7]';
  idx constant int := 0;
  j_expected constant jsonb := '[4, 5, 7]';
begin
  assert
    ((j_left - idx) - idx) - idx = j_expected,
 'assert failed';
end;
$body$;
```

## Remove a single key-value pair from an _object_ or a single value from an _array_ at the specified path: `#-`

Thus:

```postgresql
do $body$
declare
  j_left constant jsonb := '["a", {"b":17, "c": ["dog", "cat"]}]';
  path constant text[] := array['1', 'c', '0'];
  j_expected constant jsonb := '["a", {"b": 17, "c": ["cat"]}]';
begin
  assert
    j_left #- path = j_expected,
 'assert failed';
end;
$body$;
```

Just as with the `#>` and `#>>` operators, array index values are presented as convertible `text` values. Notice that the address of each JSON array element along the path is specified JSON-style, where the index starts at zero.

### Semantics when _json_in_ is an _object_

An _object_ is a set of key-value pairs where each key is unique and the order is undefined and insignificant. (As explained earlier, when a JSON manifest constant is parsed, or when two JSON values are concatenated, and if a key is repeated, then the last-mentioned in left-to-right order wins.) The functionality is sufficiently illustrated by a `json_in` value that has just primitive values. The result of each function invocation is the same.

```postgresql
do $body$
declare
  j constant jsonb := '{"a": 1, "b": 2, "c": 3}';
  path constant text[] := array['d'];
  new_number constant jsonb := '4';

  j_set constant jsonb := jsonb_set(
    jsonb_in          => j,
    path              => path,
    replacement       => new_number,
    create_if_missing => true);

  j_insert constant jsonb := jsonb_insert(
    jsonb_in          => j,
    path              => path,
    replacement       => new_number,
    insert_after      => false);

  expected_j constant jsonb := '{"a": 1, "b": 2, "c": 3, "d": 4}';

begin
  assert
    j_set = expected_j and
    j_insert = expected_j,
  'unexpected';
end;
$body$;
```

Notice that the specified `path`, the key `"d"` doesn't yet exist. Each function call asks to produce the result that the key `"d"` should exist with  the value `4`. So, as we see, the effect of each, as written above, is the same.

If `jsonb_set()` is invoked with `create_if_missing=>false`, then its result is the same as the input. But if `jsonb_insert()` is invoked with `insert_after=>true`, then its output is the same as when it's invoked with `insert_after=>false`. This reflects the fact that the order of key-value pairs in an _object_ is insignificant.

What if `path` specifies a key that does already exist? Now `jsonb_insert()` causes this error when it's invoked both with `insert_after=>true` and with `insert_after=>false`:

```
cannot replace existing key
Try using the function jsonb_set to replace key value.
```

And this `DO` block quietly succeeds, both when it's invoked with `create_if_missing=>false` and when it's invoked with `create_if_missing=>true`.

```postgresql
do $body$
declare
  j constant jsonb := '{"a": 1, "b": 2, "c": 3}';
  path constant text[] := array['c'];
  new_number constant jsonb := '4';

  j_set constant jsonb := jsonb_set(
    jsonb_in          => j,
    path              => path,
    replacement       => new_number,
    create_if_missing => true);

  expected_j constant jsonb := '{"a": 1, "b": 2, "c": 4}';

begin
  assert
    j_set = expected_j,
  'unexpected';
end;
$body$;
```

### Semantics when _json_in_ is an _array_

An _array_ is a list of index-addressable values — in other words, the order is undefined and insignificant. Again, the functionality is sufficiently illustrated by a `json_in` value that has just primitive values. Now the result of `jsonb_set()` differs from that of `jsonb_insert()`. 

```postgresql
do $body$
declare
  j constant jsonb := '["a", "b", "c", "d"]';
  path constant text[] := array['3'];
  new_string constant jsonb := '"x"';

  j_set constant jsonb := jsonb_set(
    jsonb_in          => j,
    path              => path,
    replacement       => new_string,
    create_if_missing => true);

  j_insert constant jsonb := jsonb_insert(
    jsonb_in     => j,
    path         => path,
    replacement  => new_string,
    insert_after => true);

  expected_j_set    constant jsonb := '["a", "b", "c", "x"]';
  expected_j_insert constant jsonb := '["a", "b", "c", "d", "x"]';

begin
  assert
    (j_set = expected_j_set) and
    (j_insert = expected_j_insert),
  'unexpected';
end;
$body$;
```

Notice that the path denotes the fourth value and that this already exists.

Here, `jsonb_set()` located the fourth value and set it to `"x"` while `jsonb_insert()` located the fourth value and, as requested by `insert_after=>true`, inserted `"x"` after it. Of course, with `insert_after=>false`, `"x"` is inserted before `"d"`. And (of course, again) the choice for `create_if_missing` has no effect on the result of `jsonb_set()`.

What if the path denotes a value beyond the end of the array?

```postgresql
do $body$
declare
  j constant jsonb := '["a", "b", "c", "d"]';
  path constant text[] := array['42'];
  new_string constant jsonb := '"x"';

  j_set constant jsonb := jsonb_set(
    jsonb_in          => j,
    path              => path,
    replacement       => new_string,
    create_if_missing => true);

  j_insert constant jsonb := jsonb_insert(
    jsonb_in          => j,
    path              => path,
    replacement       => new_string,
    insert_after      => true);

  expected_j constant jsonb := '["a", "b", "c", "d", "x"]';

begin
  assert
    j_set = expected_j and
    j_insert = expected_j,
  'unexpected';
end;
$body$;
```

Here, each function had the same effect.

The path, for `jsonb_set()`, is taken to mean the as yet nonexistent fifth value. So, with `create_if_missing=>false`, `jsonb_set()` has no effect.

The path, for `jsonb_insert()`, is also taken to mean the as yet nonexistent fifth value. But now, the choice of `true` or `false` for `insert_after` makes no difference because before, or after, a nonexistent element is simply taken to mean insert it.

Notice that even if the path is specified as `-42` (i.e. an impossible _array_ index) the result is the complementary. So this:

```postgresql
do $body$
declare
  j constant jsonb := '["a", "b", "c", "d"]';
  path constant text[] := array['-42'];
  new_string constant jsonb := '"x"';

  j_set constant jsonb := jsonb_set(
    jsonb_in          => j,
    path              => path,
    replacement       => new_string,
    create_if_missing => true);

  j_insert constant jsonb := jsonb_insert(
    jsonb_in          => j,
    path              => path,
    replacement       => new_string,
    insert_after      => true);

  expected_j constant jsonb := '["x", "a", "b", "c", "d"]';

begin
  assert
    j_set = expected_j and
    j_insert = expected_j,
  'unexpected';
end;
$body$;
```

The path, for `jsonb_set()`, is taken to mean a new first value (implying that the existing values all move along one place). So, again, with `create_if_missing=>false`, `jsonb_set()` has no effect.

The path, for `jsonb_insert()`, is also taken to mean a new first value. So again, the choice of `true` or `false` for `insert_after` makes no difference because before or after, a nonexsistent element is simply taken to mean insert it.
