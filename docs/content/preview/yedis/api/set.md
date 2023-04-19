---
title: SET
linkTitle: SET
description: SET
menu:
  preview:
    parent: api-yedis
    weight: 2270
aliases:
  - /preview/api/redis/set
  - /preview/api/yedis/set
type: docs
---

## Synopsis

**`SET key string_value [EX seconds] [PX milliseconds] [NX|XX]`**

This command inserts `string_value` to be hold at `key`, where `EX seconds` sets the expire time in `seconds`, `PX milliseconds` sets the expire time in `milliseconds`, `NX` sets the key only if it does not already exist, and `XX` sets the key only if it already exists.

- If the `key` is already associated with a value, it is overwritten regardless of its type.
- All parameters that are associated with the `key`, such as data type and time to live, are discarded.

## Return Value

Returns status string.

## Examples

```sh
$ SET yugakey "Yugabyte"
```

```
"OK"
```

```sh
$ GET yugakey
```

```
"Yugabyte"
```

## See also

[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`setrange`](../setrange/), [`strlen`](../strlen/)
