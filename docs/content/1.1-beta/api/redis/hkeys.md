---
title: HKEYS
linkTitle: HKEYS
description: HKEYS
menu:
  1.1-beta:
    parent: api-redis
    weight: 2140
aliases:
  - api/redis/hkeys
  - api/yedis/hkeys
---

## Synopsis
<b>`HKEYS key`</b><br>
This command fetches all fields of the hash that is associated with the given `key`.

<li>If the `key` does not exist, an empty list is returned.</li>
<li>If the `key` is associated with non-hash data, an error is raised.</li>

## Return Value
Returns list of fields in the specified hash.

## Examples
```{.sh .copy .separator-dollar}
$ HSET yugahash area1 "Africa"
```
```sh
1
```
```{.sh .copy .separator-dollar}
$ HSET yugahash area2 "America"
```
```sh
1
```
```{.sh .copy .separator-dollar}
$ HKEYS yugahash
```
```sh
1) "area1"
2) "area2"
```

## See Also
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
