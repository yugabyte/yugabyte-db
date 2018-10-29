---
title: FLUSHDB
linkTitle: FLUSHDB
description: FLUSHDB
menu:
  v1.0:
    parent: api-redis
    weight: 2065
---

## Synopsis
<b>`FLUSHDB`</b><br>
This command deletes all keys from a database.

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

## See Also
[`del`](../del/), [`flushall`](../flushall/),[`deletedb`](../deletedb/)
