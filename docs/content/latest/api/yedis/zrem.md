---
title: ZREM
linkTitle: ZREM
description: ZREM
menu:
  latest:
    parent: api-redis
    weight: 2530
aliases:
  - /latest/api/redis/zrem
  - /latest/api/yedis/zrem
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`ZREM key member [member...]`</b><br>
This command removes the `members` specified in the sorted set at `key` and returns the number of `members` removed.
`Members` specified that do not exist in key are ignored. If `key` does not exist, 0 is returned.
If `key` is associated with non sorted-set data, an error is returned.

## Return Value

The number of `members` removed from the sorted set.

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
$ ZREM z_key v2 v3
```
</div>
```sh
(integer) 1
```
<div class='copy separator-dollar'>
```sh
$ ZREM z_key v1 v2 v3
```
</div>
```sh
(integer) 1
```
<div class='copy separator-dollar'>
```sh
$ ZCARD z_key
```
</div>
```sh
(integer) 0
```
## See Also
[`zadd`](../zadd/), [`zcard`](../zcard/), [`zrange`](../zrange/), [`zrangebyscore`](../zrangebyscore/), [`zrevrange`](../zrevrange)
