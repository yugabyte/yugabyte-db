---
title: FLUSHDB
linkTitle: FLUSHDB
description: FLUSHDB
menu:
  latest:
    parent: api-redis
    weight: 2065
aliases:
  - api/redis/flushdb
  - api/yedis/flushdb
---

## SYNOPSIS
<b>`FLUSHDB`</b><br>
This command deletes all keys from a database.

## RETURN VALUE
Returns status string.

## EXAMPLES
```{.sh .copy .separator-dollar}
$ SET yuga1 "America"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ SET yuga2 "Africa"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ GET yuga1
```
```sh
"America"
```
```{.sh .copy .separator-dollar}
$ GET yuga2
```
```sh
"Africa"
```
```{.sh .copy .separator-dollar}
$ FLUSHDB
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ GET yuga1
```
```sh
(null)
```
```{.sh .copy .separator-dollar}
$ GET yuga2
```
```sh
(null)
```

## SEE ALSO
[`del`](../del/), [`flushall`](../flushall/)
