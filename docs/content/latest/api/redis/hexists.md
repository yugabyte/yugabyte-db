---
title: HEXISTS
linkTitle: HEXISTS
description: HEXISTS
menu:
  latest:
    parent: api-redis
    weight: 2110
aliases:
  - api/redis/hexists
---

## SYNOPSIS
<b>`HEXISTS key field`</b><br>
This is a predicate to check whether or not the given `field` exists in the hash that is specified by the given `key`.
<li>If the given `key` and `field` exist, 1 is returned.</li>
<li>If the given `key` or `field` does not exist, 0 is returned.</li>
<li>If the given `key` is associated with non-hash data, an error is raised.</li>

## RETURN VALUE
Returns existence status as integer, either 1 or 0.

## EXAMPLES
```{.sh .copy .separator-dollar}
$ HSET yugahash area1 "America"
```
```sh
1
```
```{.sh .copy .separator-dollar}
$ HEXISTS yugahash area1
```
```sh
1
```
```{.sh .copy .separator-dollar}
$ HEXISTS yugahash area_none
```
```sh
0
```

## SEE ALSO
[`hdel`](../hdel/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
