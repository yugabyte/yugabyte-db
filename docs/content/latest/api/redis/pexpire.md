---
title: PEXPIRE
linkTitle: PEXPIRE
description: PEXPIRE
menu:
  latest:
    parent: api-redis
    weight: 2233
aliases:
  - api/redis/pexpire
  - api/yedis/pexpire
---

## Synopsis
<b>`PEXPIRE key timeout`</b><br>
This command works exactly like EXPIRE but the time to live of the key is specified in milliseconds instead of seconds.

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
$ PEXPIRE yugakey 10000
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
[`expire`](../expire/), [`ttl`](../ttl/), [`pttl`](../pttl/), [`set`](../set/)
