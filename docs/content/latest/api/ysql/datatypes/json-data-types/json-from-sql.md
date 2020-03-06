---
title: JSON from SQL
linktitle: Create a JSON value from SQL values
summary: Concatenation: Create a JSON value from SQL values
description: Concatenation: Create a JSON value from SQL values
menu:
  latest:
    identifier: json-from-sql
    parent: functions-operators
isTocNested: true
showAsideToc: true
---

| operator/function | description |
| ---- | ---- |
| `::jsonb` | Typecasts SQL `text` value that conforms to RFC 7159 to a `jsonb` value. |
| `to_jsonb()` | Converts a single SQL value into a semantically equivalent JSON value. The SQL value can be an arbitrary tree. The intermediate nodes are either `record` (which corresponds to a JSON _object_) or `array` (which corresponds to a JSON _array_). And the terminal nodes a primitive `text`, `numeric`, `boolean`, or `null` (which correspond, respectively, to JSON _string_, _number_, _boolean_, and _null_). In the general case, the result is a JSON _object_ or JSON _array_. In the degenerate case (where the input is a primitive SQL value) the result is the corresponding primitive JSON value. |
| `row_to_json()` | A special case of `to_json` that requires that the input is a SQL `record`. The result is a JSON _object_. It has no practical advantage over `to_jsonb()`. |
| `array_to_json()` | A special case of `to_json` that requires that the input is a SQL `array`. The result is a JSON _object_. It has no practical advantage over `to_jsonb()`. |
| `jsonb_build_array()` | Variadic function that takes an arbitrary number of actual arguments of mixed SQL data types and produce a JSON _array_. Valuable because the values in a JSON _array_  can each have a different data type from the others, but the values in a SQL `array` must all have the same data type. |
| `jsonb_build_object()` | Variadic function that is the obvious counterpart to `jsonb_build_array`. The keys and values can be specified in a few different ways, for example in an alternating list of keys (as `text` values) and their values (as values of any of `text`, `numeric`, `boolean` — or `null`. |
| `jsonb_object()`     | Non-variadic function that achieves roughly the same effect as `jsonb_build_object()` with simpler syntax by presenting the key-value pairs using an array of data type `::text`. However, the functionality is severely limited because all the SQL `text` values are mapped to JSON _string_ values. |