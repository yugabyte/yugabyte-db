---
title: GETSET
linkTitle: GETSET
description: GETSET
menu:
  1.1-beta:
    parent: api-redis
    weight: 2090
aliases:
  - api/redis/getset
  - api/yedis/getset
---

## Synopsis
<b>`GETSET key value`</b><br>
This command is an atomic read and write operation that gets the existing value that is associated with the given `key` while rewriting it with the given `value`.

<li>If the given `key` does not exist, the given `value` is inserted for the `key`, and null is returned.</li>
<li>If the given `key` is associated with non-string data, an error is raised.</li>

## Return Value
Returns the old value of the given `key`.

## Examples
```{.sh .copy .separator-dollar}
$ SET yugakey 1
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ GETSET yugakey 2
```
```sh
1
```

## See Also
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`incr`](../incr/), [`incrby`](../incrby/), [`mget`](../mget/), [`mset`](../mset/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
