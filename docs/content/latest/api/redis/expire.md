---
title: EXPIRE
linkTitle: EXPIRE
description: EXPIRE
menu:
  latest:
    parent: api-redis
    weight: 2061
aliases:
  - /latest/api/redis/expire
  - /latest/api/yedis/expire
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`EXPIRE key timeout`</b><br>
Set a timeout on key (in seconds). After the timeout has expired, the key will automatically be deleted.

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
$ EXPIRE yugakey 10
```
```sh
(integer) 1
```
```{.sh .copy .separator-dollar}
$ EXPIRE non-existent-key 10
```
```sh
(integer) 0
```

## See Also
[`expireat`](../expireat/), [`ttl`](../ttl/), [`pttl`](../pttl/), [`set`](../set/)
