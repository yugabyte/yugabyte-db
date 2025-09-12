---
title: INCRBY
linkTitle: INCRBY
description: INCRBY
menu:
  preview:
    parent: api-yedis
    weight: 2215
aliases:
  - /preview/api/redis/incrby
  - /preview/api/yedis/incrby
type: docs
---

## Synopsis

**`INCRBY key delta`**

This command adds `delta` to the number that is associated with the given `key`. The numeric value must a 64-bit signed integer.

- If the `key` does not exist, the associated string is set to "0" before performing the operation.
- If the given `key` is associated with a non-string value, or if its associated string cannot be converted to an integer, an error is raised.

## Return value

Returns the value after addition.

## Examples

```sh
$ SET yugakey 7
```

```
"OK"
```

```sh
$ INCRBY yugakey 3
```

```
10
```

## See also

[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
