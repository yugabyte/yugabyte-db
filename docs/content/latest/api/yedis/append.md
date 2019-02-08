---
title: APPEND
linkTitle: APPEND
description: APPEND
menu:
  latest:
    parent: api-redis
    weight: 2010
aliases:
  - /latest/api/redis/append
  - /latest/api/yedis/append
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`APPEND key string_value`</b><br>
This command appends a value to the end of the string that is associated with the given `key`.
<li>If the `key` already exists, the given `string_value` is appended to the end of the string value that is associated with the `key`.</li>
<li>If the `key` does not exist, it is created and associated with an empty string.</li>
<li>If the `key` is associated with a non-string value, an error is raised.</li>

## Return Value
Returns the length of the resulted string after appending.

## Examples

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ GET yugakey
```
</div>
```sh
"Yuga"
```
<div class='copy separator-dollar'>
```sh
$ APPEND yugakey "Byte"
```
</div>
```sh
8
```
<div class='copy separator-dollar'>
```sh
$ GET yugakey
```
</div>
```sh
"YugaByte"
```

## See Also
[`get`](../get/), [`getrange`](../getrange/), [`getset`](../getset/), [`incr`](../incr/), [`incrby`](../incrby/), [`set`](../set/), [`setrange`](../setrange/), [`strlen`](../strlen/)
