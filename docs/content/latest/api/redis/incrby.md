---
title: INCRBY
linkTitle: INCRBY
description: INCRBY
menu:
  latest:
    parent: api-redis
    weight: 2215
aliases:
  - api/redis/incrby
  - api/yedis/incrby
---

## SYNOPSIS
<b>`INCR key`</b><br>
This command adds 1 to the number that is associated with the given `key`. The numeric value must a 64-bit signed integer.
<li>If the `key` does not exist, the associated string is set to "0".</li>
<li>If the given `key` is associated with a non-string value, or if its associated string cannot be converted to an integer, an error is raised.</li>

## RETURN VALUE
Returns the value after addition.

## EXAMPLES
```{.sh .copy .separator-dollar}
$ SET yugakey 7
```
```sh
OK
```
```{.sh .copy .separator-dollar}
$ INCRBY yugakey 3
```
```sh
10
```

## SEE ALSO
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`mget`](../mget/), [`mset`](../mset/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
