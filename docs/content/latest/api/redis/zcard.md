---
title: ZCARD
linkTitle: ZCARD
description: ZCARD
menu:
  latest:
    parent: api-redis
    weight: 2510
aliases:
  - /latest/api/redis/zcard
  - /latest/api/yedis/zcard
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`ZCARD key`</b><br>
This command returns the number of `members` in the sorted set at `key`. If `key` does not exist, 0 is returned.
If `key` is associated with non sorted-set data, an error is returned.

## Return Value

The cardinality of the sorted set.

## Examples

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ ZADD z_key 1.0 v1 2.0 v2
```
</div>
```sh
(integer) 2
```
<div class='copy separator-dollar'>
```sh
$ ZADD z_key 3.0 v2
```
</div>
```sh
(integer) 0
```
<div class='copy separator-dollar'>
```sh
$ ZCARD z_key
```
</div>
```sh
(integer) 2
```
<div class='copy separator-dollar'>
```sh
$ ZCARD ts_key
```
</div>
```sh
(integer) 0
```
## See Also
[`zadd`](../zadd/), [`zrange`](../zrange/), [`zrangebyscore`](../zrangebyscore/), [`zrem`](../zrem/), [`zrevrange`](../zrevrange)
