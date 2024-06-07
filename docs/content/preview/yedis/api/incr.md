---
title: INCR
linkTitle: INCR
description: INCR
menu:
  preview:
    parent: api-yedis
    weight: 2210
aliases:
  - /preview/api/redis/incr
  - /preview/api/yedis/incr
type: docs
---

## Synopsis

**`INCR key`**

This command adds 1 to the number that is associated with the given `key`. The numeric value must a 64-bit signed integer.

- If the `key` does not exist, the associated string is set to "0".
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
$ INCR yugakey
```

```
8
```

## See also

[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incrby`](../incrby/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
