---
title: SETRANGE
linkTitle: SETRANGE
description: SETRANGE
menu:
  preview:
    parent: api-yedis
    weight: 2280
aliases:
  - /preview/api/redis/setrange
  - /preview/api/yedis/setrange
type: docs
---

## Synopsis

**`SETRANGE key offset value`**

This command overwrites the string that is associated with the given `key` with the given `value`, starting from the given `offset`.

- The `offset` cannot exceed 536870911.
- If the `offset` is larger than the length of the specified string, the string will be padded with zeros up to the `offset`.
- If the `key` does not exist, its associated string is an empty string. The resulted new string is constructed with zeros up to the given `offset` and then appended with the given `value`.
- If the `key` is associated with a non-string value, an error is raised.

## Return value

Returns the length of the resulted string after overwriting.

## Examples

```sh
$ SET yugakey "YugaKey"
```

```
"OK"
```

```sh
$ SETRANGE yugakey 4 "Byte"
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

[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`set`](../set/), [`strlen`](../strlen/)
