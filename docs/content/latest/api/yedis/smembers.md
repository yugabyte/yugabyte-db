---
title: SMEMBERS
linkTitle: SMEMBERS
description: SMEMBERS
menu:
  latest:
    parent: api-redis
    weight: 2300
aliases:
  - /latest/api/redis/smembers
  - /latest/api/yedis/smembers
isTocNested: true
showAsideToc: true
---
## Synopsis
<b>`SMEMBERS key`</b><br>
This command selects all members of the set that is associated with the given `key`.
<li>If `key` is associated with a value that is not a set, an error is raised.</li>
<li>If `key` does not exist, no value is returned.</li>

## Return Value
Returns all members of the given set.

## Examples

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ SADD yuga_world "Africa"
```
</div>
```sh
1
```
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
$ SMEMBERS yuga_world
```
</div>
```sh
1) "Africa"
2) "America"
```

## See Also
[`sadd`](../sadd/), [`scard`](../scard/), [`sismember`](../sismember/), [`srem`](../srem/)
