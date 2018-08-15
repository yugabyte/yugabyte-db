---
title: HGET
linkTitle: HGET
description: HGET
menu:
  1.1-beta:
    parent: api-redis
    weight: 2120
aliases:
  - api/redis/hget
  - api/yedis/hget
---

## Synopsis
<b>`HGET key field`</b><br>
This command fetches the value for the given `field` in the hash that is specified by the given `key`.

<li>If the given `key` or `field` does not exist, nil is returned.</li>
<li>If the given `key` is associated with non-hash data, an error is raised.</li>

## Return Value
Returns the value for the given `field`

## Examples
```{.sh .copy .separator-dollar}
$ HSET yugahash area1 "America"
```
```sh
1
```
```{.sh .copy .separator-dollar}
$ HGET yugahash area1
```
```sh
"America"
```

## See Also
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
