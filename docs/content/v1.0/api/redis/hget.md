---
title: HGET
linkTitle: HGET
description: HGET
menu:
  v1.0:
    parent: api-redis
    weight: 2120
---

## Synopsis
<b>`HGET key field`</b><br>
This command fetches the value for the given `field` in the hash that is specified by the given `key`.

<li>If the given `key` or `field` does not exist, nil is returned.</li>
<li>If the given `key` is associated with non-hash data, an error is raised.</li>

## Return Value
Returns the value for the given `field`

## Examples

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ HSET yugahash area1 "America"
```
</div>
```sh
1
```
<div class='copy separator-dollar'>
```sh
$ HGET yugahash area1
```
</div>
```sh
"America"
```

## See Also
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
