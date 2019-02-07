---
title: TTL
linkTitle: TTL
description: TTL
menu:
  latest:
    parent: api-redis
    weight: 2470
aliases:
  - /latest/api/redis/ttl
  - /latest/api/yedis/ttl
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`TTL key`</b><br>
Returns the remaining time to live of a key that has a timeout, in seconds.

## Return Value
Returns TTL in seconds, encoded as integer response.

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
$ EXPIRE yugakey 10
```
</div>
```sh
(integer) 1
```
<div class='copy separator-dollar'>
```sh
$ TTL yugakey
```
</div>
```sh
(integer) 10
```

## See Also
[`set`](../set/), [`expire`](../expire/), [`expireat`](../expireat/), [`pttl`](../pttl/)
