---
title: SETEX
linkTitle: SETEX
description: SETEX
menu:
  preview:
    parent: api-yedis
    weight: 2271
aliases:
  - /preview/api/redis/setex
  - /preview/api/yedis/setex
type: docs
---

## Synopsis

**`SETEX key ttl_in_sec string_value`**

This command sets the value of `key` to be `string_value`, and sets the key to expire in `ttl_in_sec` seconds.

## Return value

Returns status string.

## Examples

```sh
$ SETEX yugakey 10 "Yugabyte"
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
```sh
$ TTL yugakey
```

```
(integer) 10
```

## See also

[`append`](../append/), [`set`](../set/), [`psetex`](../psetex/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`setrange`](../setrange/), [`strlen`](../strlen/)
