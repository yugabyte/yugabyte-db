---
title: INCR
linkTitle: INCR
description: INCR
menu:
  latest:
    parent: api-redis
    weight: 2210
aliases:
  - api/redis/incr
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
"OK"
```
```{.sh .copy .separator-dollar}
$ INCR yugakey
```
```sh
8
```

## SEE ALSO
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`mget`](../mget/), [`mset`](../mset/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
