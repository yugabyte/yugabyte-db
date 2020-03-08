---
title: jsonb_extract_path()
linkTitle: jsonb_extract_path()
summary: jsonb_extract_path() and json_extract_path()
description: jsonb_extract_path() and json_extract_path()
menu:
  latest:
    identifier: jsonb-extract-path
    parent: functions-operators
isTocNested: true
showAsideToc: true
---

Here is the signature for the `jsonb` variant:

```
input value        jsonb, VARIADIC text
return value       jsonb
```

These are functionally identical to the `#>` operator. The invocation of `#>` can be mechanically transformed to use `jsonb_extract_path()` by these steps:

- Add the function invocation with its required parentheses.
- Replace `#>` with a comma.
- Write the path as a comma-separated list of terms, where both integer array indexes and key-value keys are presented as `text` values, taking advantage of the fact that the function is variadic).

This `DO` block shows the invocation of `#>` and `jsonb_extract_path()` vertically to make visual comparison easy.

```
do $body$
declare
  j constant jsonb :=
    '[1, {"x": [1, true, {"a": "cat", "b": "dog"}, 3.14159], "y": true}, 42]';

  jsub_1 constant jsonb :=                    j #> array['1', 'x', '2', 'b'];
  jsub_2 constant jsonb := jsonb_extract_path(j,         '1', 'x', '2', 'b');

  expected_jsub constant jsonb := '"dog"';
begin
  assert
    (jsub_1 = expected_jsub) and
    (jsub_2 = expected_jsub),
  'unexpected';
end;
$body$;
```

Strangely, even though `jsonb_extract_path()`is variadic, each step that defines the path must be presented as a convertible SQL `text`, even when its meaning is a properly expressed by a SQL `integer`.

The function form is more verbose than the operator form. Moreover, the fact that the function is variadic makes it impossible to invoke it statically (in PL/pgSQL code) when the path length isn't known until run time. There seems, therefore, to be no reason to prefer the function form to the operator form.
