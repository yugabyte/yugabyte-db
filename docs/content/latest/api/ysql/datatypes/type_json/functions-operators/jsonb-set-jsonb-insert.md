---
title: jsonb_set()
linkTitle: jsonb_set()
summary: jsonb_set()  and jsonb_insert()
description: jsonb_set()  and jsonb_insert()
menu:
  latest:
    identifier: jsonb-set-jsonb-insert
    parent: functions-operators
    weight: 210
isTocNested: true
showAsideToc: true
---

These two functions require a `jsonb` input. There are no variants for plain `json`.

- Use `jsonb_set()` to change an existing JSON value, i.e. the value of an existing key-value pair in a JSON _object_ or the value at an existing index in a JSON array.

- Use `jsonb_insert()` to insert a new value, either as the value for a key that doesn't yet exist in a JSON _object_ or beyond the end or before the start of the index range for a JSON _array_.

It turns out that the effect of the two functions is the same in some cases. This brings useful "upsert" functionality when the target is a JSON _array_. Here are their signatures:

```
jsonb_set()
-----------
  jsonb_in           jsonb
  path               text[]
  replacement        jsonb
  create_if_missing  boolean default true
  return value       jsonb
```

and:

```
jsonb_insert()
--------------
  jsonb_in           jsonb
  path               text[]
  replacement        jsonb
  insert_after       boolean default false
  return value       jsonb
```

The meaning of the defaulted boolean formal parameter is context dependent.

The input JSON value must be either an _object_ or an _array_ — in other words, it must have elements that can be addressed by a path.
