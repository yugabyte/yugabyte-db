---
title: HGETALL
linkTitle: HGETALL
description: HGETALL
menu:
  v1.0:
    parent: api-redis
    weight: 2130
---

## Synopsis
<b>`HGETALL key`</b><br>
This command fetches the full content of all fields and all values of the hash that is associated with the given `key`.

<li>If the given `key` does not exist, and empty list is returned.</li>
<li>If the given `key` is associated with non-hash data, an error is raised.</li>

## Return Value
Returns list of fields and values.

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
$ HGETALL yugahash
```
</div>
```sh
1) area1
2) "Africa"
3) area2
4) "America"
```

## See Also
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
