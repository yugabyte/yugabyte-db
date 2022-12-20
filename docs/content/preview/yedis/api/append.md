---
title: APPEND
linkTitle: APPEND
description: APPEND
menu:
  preview:
    parent: api-yedis
    weight: 2010
aliases:
  - /preview/api/redis/append
  - /preview/api/yedis/append
type: docs
---

## Synopsis

**`APPEND key string_value`**

This command appends a value to the end of the string that is associated with the given `key`.

- If the `key` already exists, the given `string_value` is appended to the end of the string value that is associated with the `key`.
- If the `key` does not exist, it is created and associated with an empty string.
- If the `key` is associated with a non-string value, an error is raised.

## Return value

Returns the length of the resulted string after appending.

## Examples

```sh
$ GET yugakey
```

```
"Yuga"
```

```sh
$ APPEND yugakey "Byte"
```

```
8
```

```sh
$ GET yugakey
```

```
"Yugabyte"
```

## See also

[`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
