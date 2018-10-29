---
title: MSET
linkTitle: MSET
description: MSET
menu:
  v1.0:
    parent: api-redis
    weight: 2230
---

## Synopsis
<b>`MSET key value [key value ...]`</b><br>
This command is an atomic write that sets the data for all given `keys` with their associated `values`.

<li>If a `key` already exists, it is overwritten regardless of its datatype.</li>

## Return Value
Returns status string.

## Examples
```{.sh .copy .separator-dollar}
$ MSET yuga1 "Africa" yuga2 "America"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ GET yuga1
```
```sh
"Africa"
```

## See Also
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`mget`](../mget/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
