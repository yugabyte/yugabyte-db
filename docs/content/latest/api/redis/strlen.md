---
title: STRLEN
linkTitle: STRLEN
description: STRLEN
menu:
  latest:
    parent: api-redis
    weight: 2320
aliases:
  - api/redis/strlen
  - api/yedis/strlen
---

## SYNOPSIS
<b>`STRLEN key`</b><br>
This command finds the length of the string value that is associated with the given `key`.
<li> If `key` is associated with a non-string value, an error is raised.</li>
<li> If `key` does not exist, 0 is returned.</li>

## RETURN VALUE
Returns length of the specified string.

## EXAMPLES
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

## SEE ALSO
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`mget`](../mget/), [`mset`](../mset/), [`set`](../set/), [`setrange`](../setrange/)
