---
title: EXPIREAT
linkTitle: EXPIREAT
description: EXPIREAT
menu:
  latest:
    parent: api-redis
    weight: 2062
aliases:
  - /latest/api/redis/expireat
  - /latest/api/yedis/expireat
---

## Synopsis
<b>`EXPIREAT key ttl-as-timestamp`</b><br>
EXPIREAT has the same effect as EXPIRE, but instead of specifying the number of seconds representing the TTL (time to live), it takes an absolute Unix timestamp (seconds since January 1, 1970). A timestamp in the past will delete the key immediately.

## Return Value
Returns integer reply, specifically 1 if the timeout was set and 0 if key does not exist.

## Examples
```{.sh .copy .separator-dollar}
$ SET yugakey "YugaByte"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ EXPIREAT yugakey 1293840000
```
```sh
(integer) 1
```
```{.sh .copy .separator-dollar}
$ EXISTS yugakey
```
```sh
(integer) 0
```

## See Also
[`expire`](../expire/), [`ttl`](../ttl/), [`pttl`](../pttl/), [`set`](../set/) 
