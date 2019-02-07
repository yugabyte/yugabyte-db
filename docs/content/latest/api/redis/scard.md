---
title: SCARD
linkTitle: SCARD
description: SCARD
menu:
  latest:
    parent: api-redis
    weight: 2260
aliases:
  - /latest/api/redis/scard
  - /latest/api/yedis/scard
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`SCARD key`</b><br>
This command finds the cardinality of the set that is associated with the given `key`. Cardinality is the number of elements in a set.
<li>If the `key` does not exist, 0 is returned.</li>
<li>If the `key` is associated with a non-set value, an error is raised.</li>

## Return Value
Returns the cardinality of the set.

## Examples

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ SADD yuga_world "America"
```
</div>
```sh
1
```
<div class='copy separator-dollar'>
```sh
$ SADD yuga_world "Asia"
```
</div>
```sh
1
```
<div class='copy separator-dollar'>
```sh
$ SCARD yuga_world
```
</div>
```sh
2
```

## See Also
[`sadd`](../sadd/), [`sismember`](../sismember/), [`smembers`](../smembers/), [`srem`](../srem/)
