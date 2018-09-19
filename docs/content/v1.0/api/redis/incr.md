---
title: INCR
linkTitle: INCR
description: INCR
menu:
  v1.0:
    parent: api-redis
    weight: 2210
aliases:
  - api/redis/incr
  - api/yedis/incr
---

## Synopsis
<b>`INCR key`</b><br>
This command adds 1 to the number that is associated with the given `key`. The numeric value must a 64-bit signed integer.
<li>If the `key` does not exist, the associated string is set to "0".</li>
<li>If the given `key` is associated with a non-string value, or if its associated string cannot be converted to an integer, an error is raised.</li>

## Return Value
Returns the value after addition.

## Examples
```{.sh .copy .separator-dollar}
$ SET yugakey 7
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ INCR yugakey
```
```sh
8
```

## See Also
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incrby`](../incrby/), [`mget`](../mget/), [`mset`](../mset/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
