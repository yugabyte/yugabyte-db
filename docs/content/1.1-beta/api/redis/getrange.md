---
title: GETRANGE
linkTitle: GETRANGE
description: GETRANGE
menu:
  1.1-beta:
    parent: api-redis
    weight: 2080
aliases:
  - api/redis/getrange
  - api/yedis/getrange
---

## Synopsis
<b>`GETRANGE key start end`</b><br>
This command fetches a substring of the string value that is associated with the given `key` between the two given offsets `start` and `end`. If an offset value is positive, it is counted from the beginning of the string. If an offset is negative, it is counted from the end. If an offset is out of range, it is limited to either the beginning or the end of the string.
<li>If `key` does not exist, (null) is returned.</li>
<li>If `key` is associated with a non-string value, an error is raised.</li>

## Return Value
Returns a string value.

## Examples
```{.sh .copy .separator-dollar}
$ SET yugakey "YugaByte"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ GETRANGE yugakey 0 3
```
```sh
"Yuga"
```
```{.sh .copy .separator-dollar}
$ GETRANGE yugakey -4 -1
```
```sh
"Byte"
```

## See Also
[`append`](../append/), [`get`](../get/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`mget`](../mget/), [`mset`](../mset/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
