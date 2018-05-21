---
title: MSET
linkTitle: MSET
description: MSET
menu:
  latest:
    parent: api-redis
    weight: 2230
aliases:
  - api/redis/mset
  - api/yedis/mset
---

## SYNOPSIS
<b>`MSET key value [key value ...]`</b><br>
This command is an atomic write that sets the data for all given `keys` with their associated `values`.

<li>If a `key` already exists, it is overwritten regardless of its datatype.</li>

## RETURN VALUE
Returns status string.

## EXAMPLES
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

## SEE ALSO
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`mget`](../mget/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
