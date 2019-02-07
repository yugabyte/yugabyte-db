---
title: HLEN
linkTitle: HLEN
description: HLEN
menu:
  latest:
    parent: api-redis
    weight: 2150
aliases:
  - /latest/api/redis/hlen
  - /latest/api/yedis/hlen
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`HLEN key`</b><br>
This command fetches the number of fields in the hash that is associated with the given `key`.
<li>If the `key` does not exist, 0 is returned.</li>
<li>If the `key` is associated with non-hash data, an error is raised.</li>

## Return Value
Returns number of fields in the specified hash.

## Examples

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ HSET yugahash area1 "Africa"
```
</div>
```sh
1
```
<div class='copy separator-dollar'>
```sh
$ HSET yugahash area2 "America"
```
</div>
```sh
1
```
<div class='copy separator-dollar'>
```sh
$ HLEN yugahash
```
</div>
```sh
2
```

## See Also
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
