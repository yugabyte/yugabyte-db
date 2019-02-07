---
title: HSET
linkTitle: HSET
description: HSET
menu:
  v1.0:
    parent: api-redis
    weight: 2180
---

## Synopsis
<b>`HSET key field value`</b><br>
This command sets the data for the given `field` of the hash that is associated with the given `key` with the given `value`. If the `field` already exists in the hash, it is overwritten.

<li>If the given `key` does not exist, an associated hash is created, and the `field` and `value` are inserted.</li>
<li>If the given `key` is not associated with a hash, an error is raised.</li>

## Return Value
Depends on the configuration parameter `emulate_redis_responses`.
<li>
If `emulate_redis_responses` is true, returns
 1 if a new field is inserted and 0 if an existing field is updated.
</li>
<li>
If `emulate_redis_responses` is false, returns
 OK
</li>


## Examples
<li> `emulate_redis_responses` is `true`.
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
$ HSET yugahash area1 "North America"
```
</div>
```sh
0
```
<div class='copy separator-dollar'>
```sh
$ HGET yugahash area1
```
</div>
```sh
"North America"
```
</li>

<li> `emulate_redis_responses` is `false`.
<div class='copy separator-dollar'>
```sh
$ HSET yugahash area1 "America"
```
</div>
```sh
OK
```
<div class='copy separator-dollar'>
```sh
$ HSET yugahash area1 "North America"
```
</div>
```sh
OK
```
<div class='copy separator-dollar'>
```sh
$ HGET yugahash area1
```
</div>
```sh
"North America"
```
</li>

## See Also
[`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hincrby`](../hincrby/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
