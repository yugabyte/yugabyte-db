---
title: FLUSHALL
linkTitle: FLUSHALL
description: FLUSHALL
menu:
  1.0:
    parent: api-redis
    weight: 2064
aliases:
  - api/redis/flushall
  - api/yedis/flushall
---

## Synopsis
<b>`FLUSHALL`</b><br>
This command deletes all keys from all databases.

## Return Value
Returns status string.

## Examples
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
$ FLUSHALL
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

## See Also
[`del`](../del/), [`flushdb`](../flushdb/)
