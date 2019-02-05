---
title: PEXPIREAT
linkTitle: PEXPIREAT
description: PEXPIREAT
menu:
  latest:
    parent: api-redis
    weight: 2234
aliases:
  - /latest/api/redis/pexpireat
  - /latest/api/yedis/pexpireat
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`PEXPIREAT key ttl-as-timestamp`</b><br>
PEXPIREAT has the same effect as EXPIREAT, but the Unix timestamp at which the key will expire is specified in milliseconds instead of seconds.

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
$ PEXPIREAT yugakey 1555555555005
```
```sh
(integer) 1
```
```{.sh .copy .separator-dollar}
$ PTTL yugakey
```
```sh
(integer) 18674452994
```

## See Also
[`expireat`](../expireat/), [`ttl`](../ttl/), [`pttl`](../pttl/), [`set`](../set/) 
