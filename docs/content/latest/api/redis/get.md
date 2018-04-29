---
title: GET
linkTitle: GET
description: GET
menu:
  latest:
    parent: api-redis
    weight: 2070
aliases:
  - api/redis/get
  - api/yedis/get
---

## SYNOPSIS
<b>`GET key`</b><br>
This command fetches the value that is associated with the given `key`.

<li>If the `key` does not exist, null is returned.</li>
<li>If the `key` is associated with a non-string value, an error is raised.</li>

## RETURN VALUE
Returns string value of the given `key`.

## EXAMPLES
```{.sh .copy .separator-dollar}
$ GET yugakey
```
```sh
(null)
```
```{.sh .copy .separator-dollar}
$ SET yugakey "YugaByte"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ GET yugakey
"YugaByte"
```

## SEE ALSO
[`append`](../append/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`mget`](../mget/), [`mset`](../mset/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
