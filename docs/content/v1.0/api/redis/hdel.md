---
title: HDEL
linkTitle: HDEL
description: HDEL
menu:
  v1.0:
    parent: api-redis
    weight: 2100
---

## Synopsis
<b>`HDEL key field [field ...]`</b><br>
This command removes the given `fields` from the hash that is associated with the given `key`.

<li>If the given `key` does not exist, it is characterized as an empty hash, and 0 is returned for no elements are removed.</li>
<li>If the given `key` is associated with non-hash data, an error is raised.</li>

## Return Value
Depends on the configuration parameter `emulate_redis_responses`.
<li>
If `emulate_redis_responses` is `true`, returns
the number of existing fields in the hash that were removed by this command.
</li>
<li>
If `emulate_redis_responses` is `false`, returns OK.
</li>


## Examples
<li> `emulate_redis_responses` is `true`.
<div class='copy separator-dollar'>
```sh
$ HSET yugahash moon "Moon"
```
</div>
```sh
1
```
<div class='copy separator-dollar'>
```sh
$ HDEL yugahash moon
```
</div>
```sh
1
```
<div class='copy separator-dollar'>
```sh
$ HDEL yugahash moon
```
</div>
```sh
0
```
</li>

<li> `emulate_redis_responses` is `false`.
<div class='copy separator-dollar'>
```sh
$ HSET yugahash moon "Moon"
```
</div>
```sh
OK
```
<div class='copy separator-dollar'>
```sh
$ HDEL yugahash moon
```
</div>
```sh
OK
```
<div class='copy separator-dollar'>
```sh
$ HDEL yugahash moon
```
</div>
```sh
OK
```
</li>

## See Also
[`hexists`](../hexists/), [`hget`](../hget/), [`hgetall`](../hgetall/), [`hkeys`](../hkeys/), [`hlen`](../hlen/), [`hmget`](../hmget/), [`hmset`](../hmset/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`hstrlen`](../hstrlen/), [`hvals`](../hvals/)
