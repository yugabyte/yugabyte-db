---
title: DEL
linkTitle: DEL
description: DEL
menu:
  v1.0:
    parent: api-redis
    weight: 2040
---
Early Releases: Single-key request only. Requests with multiple keys are not yet supported.

## Synopsis
<!-- <b>`DEL key [key ...]`</b><br> -->
<b>`DEL key`</b><br>
This command deletes the given `key`.

<li>If the `key` does not exist, it is ignored and not counted toward the total number of removed keys.</li>

## Return Value
Returns number of keys that were removed.

## Examples
```{.sh .copy .separator-dollar}
$ SET yuga1 "America"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ SET yuga2 "Africa"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ DEL yuga1
```
```sh
1
```
```{.sh .copy .separator-dollar}
$ DEL not_a_key
```
```sh
0
```
```{.sh .copy .separator-dollar}
$ DEL yuga1 yuga2
```
```sh
"ERR del: Wrong number of arguments"
```

## See Also
[`exists`](../exists/), [`flushall`](../flushall/), [`flushdb`](../flushdb/), [`get`](../get/), [`getrange`](../getrange/), [`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`sadd`](../sadd/), [`set`](../set/)
