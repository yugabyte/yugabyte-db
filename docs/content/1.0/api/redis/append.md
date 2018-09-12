---
title: APPEND
linkTitle: APPEND
description: APPEND
menu:
  1.0:
    parent: api-redis
    weight: 2010
aliases:
  - /api/redis/append
  - /api/yedis/append
---

## Synopsis
<b>`APPEND key string_value`</b><br>
This command appends a value to the end of the string that is associated with the given `key`.
<li>If the `key` already exists, the given `string_value` is appended to the end of the string value that is associated with the `key`.</li>
<li>If the `key` does not exist, it is created and associated with an empty string.</li>
<li>If the `key` is associated with a non-string value, an error is raised.</li>

## Return Value
Returns the length of the resulted string after appending.

## Examples
```{.sh .copy .separator-dollar}
$ GET yugakey
```
```sh
"Yuga"
```
```{.sh .copy .separator-dollar}
$ APPEND yugakey "Byte"
```
```sh
8
```
```{.sh .copy .separator-dollar}
$ GET yugakey
```
```sh
"YugaByte"
```

## See Also
[`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`mget`](../mget/), [`mset`](../mset/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
