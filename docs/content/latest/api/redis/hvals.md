---
title: HVALS
linkTitle: HVALS
menu:
  latest:
    parent: api-redis
    weight: 2200
aliases:
  - api/redis/hvals
---

## SYNOPSIS
<b>`HVALS key`</b><br>
This command selects all the values in the hash that is associated with the given `key`.

<li>If the `key` does not exist, an empty list is returned.</li>
<li>if the `key` is associated with a non-hash data, an error is raised.</li>

## RETURN VALUE
Returns list of values in the specified hash.

## EXAMPLES
```{.sh .copy .separator-dollar}
$ HMSET yugahash area1 "America" area2 "Africa"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ HVALS yugahash
```
```sh
1) "America"
2) "Africa"
```

## SEE ALSO
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hstrlen`](../hstrlen/)
