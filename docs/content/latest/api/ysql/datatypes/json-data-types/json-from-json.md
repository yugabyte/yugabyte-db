---
title: Create a JSON value from an existing JSON value
linktitle: Create a JSON value from an existing JSON value
summary: Create a JSON value from an existing JSON value
description: Create a JSON value from an existing JSON value
menu:
  latest:
    identifier: json-from-json
    parent: functions-operators
isTocNested: true
showAsideToc: true
---

| operator/function | description |
| ---- | ---- |
|   `->`   |   Reads a subvalue at a specified JSON _object_ key or JSON _array_ index as a JSON value.   |
| `#>` | Like `->` except that the to-be-read JSON subvalue is specified by the path to it from the enclosing JSON value. |
| `||` | Concatenates two JSON values to produce a new JSON value. |
| `-` | Creates a new JSON value from the input JSON value: _either_ by removing a key-value pair with the specified key from a JSON _object_; _or_ by removing a JSON value at the specified index in a JSON _array_. Error if the input is not a JSON _object_ or JSON _array_. |
| `#-` | Like `-` except that the to-be-removed key-value pair (from a JSON _object_) or JSON value (from a JSON _array_) is specified by a path from the enclosing JSON value. The path is specified in the same way as for the `#>` operator. |
| `jsonb_extract_path()` | Functionally equivalent to the `#>` operator. The path is presented as a variadic list of steps that must all be `text` values. Its invocation more verbose than that of the `#>` operator and there seems to be no reason to prefer the function form to the operator form. |
| `jsonb_strip_nulls()` | Finds all key-value pairs at any depth in the hierarchy of the supplied JSON compound value (such a pair can occur only as an element of an _object_) and return a JSON value where each pair whose value is _null_. has been removed. By definition, they leave _null_ values within _arrays_ untouched. |
| `jsonb_set()` and `jsonb_insert()` | These functions return a new JSON value modified from the input value in the specified way using the so-called replacement JSON value. Because the effect of `jsonb_set` is identical to that of `jsonb_insert` in some cases, they are grouped together here. However, in other cases, there are critical differences. They target a specific JSON value at a specified path. When the target is a key-value pair in a JSON _object_, they set it to the specified value when the key already exists and create it when it doesn't. When the target is a JSON value at a specified index in a JSON _array_, and the index already exists, they either set it or insert a new value after or before it. And when the index is before the _array_'s first value or after its last value, they insert it. |