---
title: HSTRLEN
linkTitle: HSTRLEN
description: HSTRLEN
menu:
  v1.0:
    parent: api-redis
    weight: 2190
---

## Synopsis
<b>`HSTRLEN key field`</b><br>
This command seeks the length of a string value that is associated with the given `field` in a hash table that is associated with the given `key`.
<li>If the `key` or `field` does not exist, 0 is returned.</li>
<li>If the `key` is associated with a non-hash-table value, an error is raised.</li>

## Return Value
Returns the length of the specified string.

## Examples

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ HMSET yugahash L1 America L2 Europe
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ HSTRLEN yugahash L1
```
</div>
```sh
7
```

## See Also
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hvals`](../hvals/)
