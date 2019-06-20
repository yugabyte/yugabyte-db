---
title: SETEX
linkTitle: SETEX
description: SETEX
menu:
  latest:
    parent: api-yedis
    weight: 2271
aliases:
  - /latest/api/redis/setex
  - /latest/api/yedis/setex
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`SETEX key ttl_in_sec string_value`</b><br>
This command sets the value of `key` to be `string_value`, and sets the key to expire in `ttl_in_sec` seconds.

## Return Value
Returns status string.

## Examples

```sh
$ SETEX yugakey 10 "YugaByte"
```

```
"OK"
```

```sh
$ GET yugakey
```

```
"YugaByte"
```
```sh
$ TTL yugakey
```

```
(integer) 10
```

## See Also
[`append`](../append/), [`set`](../set/), [`psetex`](../psetex/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`setrange`](../setrange/), [`strlen`](../strlen/)
