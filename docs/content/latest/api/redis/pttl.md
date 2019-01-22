---
title: PTTL
linkTitle: PTTL
description: PTTL
menu:
  latest:
    parent: api-redis
    weight: 2235
aliases:
  - /latest/api/redis/pttl
  - /latest/api/yedis/pttl
---

## Synopsis
<b>`PTTL key`</b><br>
Similar to TTL this command returns the remaining time to live of a key that has a timeout set, with the sole difference that TTL returns the amount of remaining time in seconds while PTTL returns it in milliseconds.

## Return Value
Returns TTL in milliseconds, encoded as integer response.

## Examples
```{.sh .copy .separator-dollar}
$ SET yugakey "YugaByte"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ EXPIRE yugakey 10
```
```sh
(integer) 1
```
```{.sh .copy .separator-dollar}
$ PTTL yugakey
```
```sh
(integer) 9995
```

## See Also
[`ttl`](../ttl/), [`set`](../set/), [`expire`](../expire/), [`expireat`](../expireat/)
