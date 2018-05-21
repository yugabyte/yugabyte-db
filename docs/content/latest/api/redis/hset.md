---
title: HSET
linkTitle: HSET
description: HSET
menu:
  latest:
    parent: api-redis
    weight: 2180
aliases:
  - api/redis/hset
  - api/yedis/hset
---

## SYNOPSIS
<b>`HSET key field value`</b><br>
This command sets the data for the given `field` of the hash that is associated with the given `key` with the given `value`. If the `field` already exists in the hash, it is overwritten.

<li>If the given `key` does not exist, an associated hash is created, and the `field` and `value` are inserted.</li>
<li>If the given `key` is not associated with a hash, an error is raised.</li>

## RETURN VALUE
Depends on the configuration parameter `emulate_redis_responses`.
<li>
If `emulate_redis_responses` is true, returns
 1 if a new field is inserted and 0 if an existing field is updated.
</li>
<li>
If `emulate_redis_responses` is false, returns
 OK
</li>


## EXAMPLES
<li> `emulate_redis_responses` is `true`.
```{.sh .copy .separator-dollar}
$ HSET yugahash area1 "America"
```
```sh
1
```
```{.sh .copy .separator-dollar}
$ HSET yugahash area1 "North America"
```
```sh
0
```
```{.sh .copy .separator-dollar}
$ HGET yugahash area1
```
```sh
"North America"
```
</li>

<li> `emulate_redis_responses` is `false`.
```{.sh .copy .separator-dollar}
$ HSET yugahash area1 "America"
```
```sh
OK
```
```{.sh .copy .separator-dollar}
$ HSET yugahash area1 "North America"
```
```sh
OK
```
```{.sh .copy .separator-dollar}
$ HGET yugahash area1
```
```sh
"North America"
```
</li>

## SEE ALSO
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hincrby`](../hincrby/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
