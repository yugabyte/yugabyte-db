---
title: INCR
linkTitle: INCR
description: INCR
menu:
  latest:
    parent: api-redis
    weight: 2210
aliases:
  - /latest/api/redis/incr
  - /latest/api/yedis/incr
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`INCR key`</b><br>
This command adds 1 to the number that is associated with the given `key`. The numeric value must a 64-bit signed integer.
<li>If the `key` does not exist, the associated string is set to "0".</li>
<li>If the given `key` is associated with a non-string value, or if its associated string cannot be converted to an integer, an error is raised.</li>

## Return Value
Returns the value after addition.

## Examples

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ SET yugakey 7
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ INCR yugakey
```
</div>
```sh
8
```

## See Also
[`append`](../append/), [`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incrby`](../incrby/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
