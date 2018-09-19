---
title: STRLEN
linkTitle: STRLEN
description: STRLEN
menu:
  v1.0:
    parent: api-redis
    weight: 2320
aliases:
  - api/redis/strlen
  - api/yedis/strlen
---

## Synopsis
<b>`STRLEN key`</b><br>
This command finds the length of the string value that is associated with the given `key`.
<li> If `key` is associated with a non-string value, an error is raised.</li>
<li> If `key` does not exist, 0 is returned.</li>

## Return Value
Returns length of the specified string.

## Examples
```{.sh .copy .separator-dollar}
$ SET yugakey "string value"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ STRLEN yugakey
```
```sh
12
```
```{.sh .copy .separator-dollar}
$ STRLEN undefined_key
```
```sh
0
```

## See Also
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`mget`](../mget/), [`mset`](../mset/), [`set`](../set/), [`setrange`](../setrange/)
