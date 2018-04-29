---
title: EXISTS
linkTitle: EXISTS
description: EXISTS
menu:
  latest:
    parent: api-redis
    weight: 2060
aliases:
  - api/redis/exist
  - api/yedis/exist
---
Early Releases: Single-key request only. Requests with multiple keys are not yet supported.

## SYNOPSIS
<b>`EXISTS key [key ...]`</b><br>
This command is a predicate to check whether or not the given `key` exists.

## RETURN VALUE
Returns the number of existing keys.

## EXAMPLES
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

## SEE ALSO
[`del`](../del/), [`get`](../get/), [`getrange`](../getrange/), [`hdel`](../hdel/), [`hexists`](../hexists/), [`hget`](../hget/), [`hset`](../hset/), [`mget`](../mget/), [`mset`](../mset/), [`sadd`](../sadd/), [`set`](../set/)
