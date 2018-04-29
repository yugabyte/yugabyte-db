---
title: HLEN
linkTitle: HLEN
description: HLEN
menu:
  latest:
    parent: api-redis
    weight: 2150
aliases:
  - api/redis/hlen
  - api/yedis/hlen
---

## SYNOPSIS
<b>`HLEN key`</b><br>
This command fetches the number of fields in the hash that is associated with the given `key`.
<li>If the `key` does not exist, 0 is returned.</li>
<li>If the `key` is associated with non-hash data, an error is raised.</li>

## RETURN VALUE
Returns number of fields in the specified hash.

## EXAMPLES
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
$ HLEN yugahash
```
```sh
2
```

## SEE ALSO
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
