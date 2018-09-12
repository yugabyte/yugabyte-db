---
title: ZREM
linkTitle: ZREM
description: ZREM
menu:
  1.0:
    parent: api-redis
    weight: 2530
aliases:
  - api/redis/zrem
  - api/yedis/zrem
---

## Synopsis
<b>`ZREM key member [member...]`</b><br>
This command removes the `members` specified in the sorted set at `key` and returns the number of `members` removed.
`Members` specified that do not exist in key are ignored. If `key` does not exist, 0 is returned.
If `key` is associated with non sorted-set data, an error is returned.

## Return Value

The number of `members` removed from the sorted set.

## Examples
```{.sh .copy .separator-dollar}
$ ZADD z_key 1.0 v1 2.0 v2
```
```sh
(integer) 2
```
```{.sh .copy .separator-dollar}
$ ZREM z_key v2 v3
```
```sh
(integer) 1
```
```{.sh .copy .separator-dollar}
$ ZREM z_key v1 v2 v3
```
```sh
(integer) 1
```
```{.sh .copy .separator-dollar}
$ ZCARD z_key
```
```sh
(integer) 0
```
## See Also
[`zadd`](../zadd/), [`zcard`](../zcard/), [`zrange`](../zrange/), [`zrangebyscore`](../zrangebyscore/), [`zrevrange`](../zrevrange)
