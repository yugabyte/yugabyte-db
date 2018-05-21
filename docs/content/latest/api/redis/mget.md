---
title: MGET
linkTitle: MGET
description: MGET
menu:
  latest:
    parent: api-redis
    weight: 2220
aliases:
  - api/redis/mget
  - api/yedis/mget
---

## SYNOPSIS
<b>`MGET key [key ...]`</b><br>
This command collects string values of all given keys.
<li>If a given `key` does not exist, an empty string is returned for that `key`.</li>
<li>If a given `key` is associated with a non-string value, an empty string is returned for that `key`.</li>

## RETURN VALUE
Returns an array of string values.

## EXAMPLES
```{.sh .copy .separator-dollar}
$ MGET yuga_area1 yuga_area2 yuga_none
```
```sh
1) "Africa"
2) "America"
3) (null)
```

## SEE ALSO
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`mset`](../mset/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
