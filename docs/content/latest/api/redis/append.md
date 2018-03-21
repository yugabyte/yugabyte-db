---
title: APPEND
linkTitle: APPEND
description: APPEND
menu:
  latest:
    parent: api-redis
    weight: 2010
aliases:
  - /api/redis/append
---

## SYNOPSIS
<b>`APPEND key string_value`</b><br>
This command appends a value to the end of the string that is associated with the given `key`.
<li>If the `key` already exists, the given `string_value` is appended to the end of the string value that is associated with the `key`.</li>
<li>If the `key` does not exist, it is created and associated with an empty string.</li>
<li>If the `key` is associated with a non-string value, an error is raised.</li>

## RETURN VALUE
Returns the length of the resulted string after appending.

## EXAMPLES
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

## SEE ALSO
[`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`mget`](../mget/), [`mset`](../mset/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)