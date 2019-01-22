---
title: ZRANGEBYSCORE
linkTitle: ZRANGEBYSCORE
description: ZRANGEBYSCORE
menu:
  latest:
    parent: api-redis
    weight: 2520
aliases:
  - /latest/api/redis/zrangebyscore
  - /latest/api/yedis/zrangebyscore
---

## Synopsis
<b>`ZRANGEBYSCORE key min max [WITHSCORES] [LIMIT offset count]`</b><br>
This command fetches `members` for which `score` is in the given `min` `max` range. `min` and `max` are doubles.
If `key` does not exist, an empty range is returned. If `key` corresponds to a non
sorted-set, an error is raised. Special bounds `-inf` and `+inf` are also supported to retrieve an entire range.
`min` and `max` are inclusive unless they are prefixed with `(`, in which case they are
exclusive. 

## Return Value
Returns a list of `members` found in the range specified by `min`, `max`, unless the WITHSCORES option is specified (see below).

## ZRANGEBYSCORE Options
<li> WITHSCORES: Makes the command return both the `member` and its `score`.</li>
<li> LIMIT offset count: Get a range of the matching elements starting at `offset` up to `count`
elements, where `offset` is 0 indexed.</li>

## Examples
```{.sh .copy .separator-dollar}
$ ZADD z_key 1.0 v1 2.0 v2
```
```sh
(integer) 2
```
Retrieve all members.
```{.sh .copy .separator-dollar}
$ ZRANGEBYSCORE z_key -inf +inf
```
```sh
1) "v1"
2) "v2"
```
Retrieve all member score pairs.
```{.sh .copy .separator-dollar}
$ ZRANGEBYSCORE z_key -inf +inf WITHSCORES
```
```sh
1) "v1"
2) "1.0"
3) "v2"
4) "2.0"
```
Bounds are inclusive.
```{.sh .copy .separator-dollar}
$ ZRANGEBYSCORE z_key 1.0 2.0
```
```sh
1) "v1"
2) "v2"
```
Bounds are exclusive.
```{.sh .copy .separator-dollar}
$ ZRANGEBYSCORE z_key (1.0 (2.0
```
```sh
(empty list or set)
```
Enforce a limit.
```{.sh .copy .separator-dollar}
ZRANGEBYSCORE z_key -inf +inf LIMIT 1 1
```
```sh
1) "v2"
```

## See Also
[`zadd`](../zadd/), [`zcard`](../zcard/), [`zrange`](../zrange/), [`zrem`](../zrem/), [`zrevrange`](../zrevrange)
