---
title: PEXPIRE
linkTitle: PEXPIRE
description: PEXPIRE
menu:
  latest:
    parent: api-redis
    weight: 2233
aliases:
  - /latest/api/redis/pexpire
  - /latest/api/yedis/pexpire
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`PEXPIRE key timeout`</b><br>
This command works exactly like EXPIRE but the time to live of the key is specified in milliseconds instead of seconds.

## Return Value
Returns integer reply, specifically 1 if the timeout was set and 0 if key does not exist.

## Examples

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ SET yugakey "YugaByte"
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ PEXPIRE yugakey 10000
```
</div>
```sh
(integer) 1
```
<div class='copy separator-dollar'>
```sh
$ PTTL yugakey
```
</div>
```sh
(integer) 9995
```

## See Also
[`expire`](../expire/), [`ttl`](../ttl/), [`pttl`](../pttl/), [`set`](../set/)
