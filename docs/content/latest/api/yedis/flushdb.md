---
title: FLUSHDB
linkTitle: FLUSHDB
description: FLUSHDB
menu:
  latest:
    parent: api-redis
    weight: 2065
aliases:
  - /latest/api/redis/flushdb
  - /latest/api/yedis/flushdb
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`FLUSHDB`</b><br>
This command deletes all keys from a database.

This functionality can be disabled by setting the yb-tserver gflag `yedis_enable_flush` to `false`.

## Return Value
Returns status string.

## Examples

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ SET yuga1 "America"
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ SET yuga2 "Africa"
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ GET yuga1
```
</div>
```sh
"America"
```
<div class='copy separator-dollar'>
```sh
$ GET yuga2
```
</div>
```sh
"Africa"
```
<div class='copy separator-dollar'>
```sh
$ FLUSHDB
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ GET yuga1
```
</div>
```sh
(null)
```
<div class='copy separator-dollar'>
```sh
$ GET yuga2
```
</div>
```sh
(null)
```

## See Also
[`del`](../del/), [`flushall`](../flushall/),[`deletedb`](../deletedb/)
