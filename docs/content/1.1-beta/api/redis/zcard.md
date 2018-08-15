---
title: ZCARD
linkTitle: ZCARD
description: ZCARD
menu:
  1.1-beta:
    parent: api-redis
    weight: 2510
aliases:
  - api/redis/zcard
  - api/yedis/zcard
---

## Synopsis
<b>`ZCARD key`</b><br>
This command returns the number of `members` in the sorted set at `key`. If `key` does not exist, 0 is returned.
If `key` is associated with non sorted-set data, an error is returned.

## Return Value

The cardinality of the sorted set.

## Examples
```{.sh .copy .separator-dollar}
$ ZADD z_key 1.0 v1 2.0 v2
```
```sh
(integer) 2
```
```{.sh .copy .separator-dollar}
$ ZADD z_key 3.0 v2
```
```sh
(integer) 0
```
```{.sh .copy .separator-dollar}
$ ZCARD z_key
```
```sh
(integer) 2
```
```{.sh .copy .separator-dollar}
$ ZCARD ts_key
```
```sh
(integer) 0
```
## See Also
[`zadd`](../zadd/), [`zrange`](../zrange/), [`zrangebyscore`](../zrangebyscore/), [`zrem`](../zrem/), [`zrevrange`](../zrevrange)
