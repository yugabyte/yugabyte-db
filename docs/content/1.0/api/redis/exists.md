---
title: EXISTS
linkTitle: EXISTS
description: EXISTS
menu:
  1.0:
    parent: api-redis
    weight: 2060
aliases:
  - api/redis/exist
  - api/yedis/exist
---
Early Releases: Single-key request only. Requests with multiple keys are not yet supported.

## Synopsis
<b>`EXISTS key [key ...]`</b><br>
This command is a predicate to check whether or not the given `key` exists.

## Return Value
Returns the number of existing keys.

## Examples
```{.sh .copy .separator-dollar}
$ SET yuga1 "Africa"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ SET yuga2 "America"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ EXISTS yuga1
```
```sh
1
```
```{.sh .copy .separator-dollar}
$ EXISTS yuga1 yuga2 not_a_key
```
```sh
2
```

## See Also
[`del`](../del/), [`get`](../get/), [`getrange`](../getrange/), [`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hset`](../hset/), [`hincrby`](../hincrby/), [`mget`](../mget/), [`mset`](../mset/), [`sadd`](../sadd/), [`set`](../set/)
