---
title: GETRANGE
linkTitle: GETRANGE
description: GETRANGE
menu:
  preview:
    parent: api-yedis
    weight: 2080
aliases:
  - /preview/api/redis/getrange
  - /preview/api/yedis/getrange
type: docs
---

## Synopsis

**`GETRANGE key start end`**

This command fetches a substring of the string value that is associated with the given `key` between the two given offsets `start` and `end`. If an offset value is positive, it is counted from the beginning of the string. If an offset is negative, it is counted from the end. If an offset is out of range, it is limited to either the beginning or the end of the string.

- If `key` does not exist, (null) is returned.
- If `key` is associated with a non-string value, an error is raised.

## Return value

Returns a string value.

## Examples

```sh
$ SET yugakey "Yugabyte"
```

```
"OK"
```

```sh
$ GETRANGE yugakey 0 3
```

```
"Yuga"
```

```sh
$ GETRANGE yugakey -4 -1
```

```
"Byte"
```

## See also

[`append`](../append/), [`get`](../get/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
