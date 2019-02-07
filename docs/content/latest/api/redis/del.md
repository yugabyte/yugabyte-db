---
title: DEL
linkTitle: DEL
description: DEL
menu:
  latest:
    parent: api-redis
    weight: 2040
aliases:
  - /latest/api/redis/del
  - /latest/api/yedis/del
isTocNested: true
showAsideToc: true
---
Single-key request only. Requests with multiple keys are not yet supported.

## Synopsis
<!-- <b>`DEL key [key ...]`</b><br> -->
<b>`DEL key`</b><br>
This command deletes the given `key`.

<li>If the `key` does not exist, it is ignored and not counted toward the total number of removed keys.</li>

## Return Value
Returns number of keys that were removed.

## Examples

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ SET yuga1 "America"
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ SET yuga2 "Africa"
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ DEL yuga1
```
</div>
```sh
1
```
<div class='copy separator-dollar'>
```sh
$ DEL not_a_key
```
</div>
```sh
0
```
<div class='copy separator-dollar'>
```sh
$ DEL yuga1 yuga2
```
</div>
```sh
"ERR del: Wrong number of arguments"
```

## See Also
[`exists`](../exists/), [`flushall`](../flushall/), [`flushdb`](../flushdb/), [`get`](../get/), [`getrange`](../getrange/), [`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`sadd`](../sadd/), [`set`](../set/)
