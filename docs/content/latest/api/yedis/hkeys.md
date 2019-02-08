---
title: HKEYS
linkTitle: HKEYS
description: HKEYS
menu:
  latest:
    parent: api-redis
    weight: 2140
aliases:
  - /latest/api/redis/hkeys
  - /latest/api/yedis/hkeys
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`HKEYS key`</b><br>
This command fetches all fields of the hash that is associated with the given `key`.

<li>If the `key` does not exist, an empty list is returned.</li>
<li>If the `key` is associated with non-hash data, an error is raised.</li>

## Return Value
Returns list of fields in the specified hash.

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
$ HKEYS yugahash
```
</div>
```sh
1) "area1"
2) "area2"
```

## See Also
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
