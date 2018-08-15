---
title: DEL
linkTitle: DEL
description: DEL
menu:
  1.1-beta:
    parent: api-redis
    weight: 2040
aliases:
  - api/redis/del
  - api/yedis/del
---
Early Releases: Single-key request only. Requests with multiple keys are not yet supported.

## Synopsis
<b>`DEL key [key ...]`</b><br>
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
$ DEL yuga1 yuga2 not_a_key
```
```sh
2
```

## See Also
[`exists`](../exists/), [`flushall`](../flushall/), [`flushdb`](../flushdb/), [`get`](../get/), [`getrange`](../getrange/), [`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`mget`](../mget/), [`mset`](../mset/), [`sadd`](../sadd/), [`set`](../set/)
