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
---
## Synopsis
<b>`SMEMBERS key`</b><br>
This command selects all members of the set that is associated with the given `key`.
<li>If `key` is associated with a value that is not a set, an error is raised.</li>
<li>If `key` does not exist, no value is returned.</li>

## Return Value
Returns all members of the given set.

## Examples
```{.sh .copy .separator-dollar}
$ SADD yuga_world "Africa"
```
```sh
1
```
```{.sh .copy .separator-dollar}
$ SADD yuga_world "America"
```
```sh
1
```
```{.sh .copy .separator-dollar}
$ SMEMBERS yuga_world
```
```sh
1) "Africa"
2) "America"
```

## See Also
[`sadd`](../sadd/), [`scard`](../scard/), [`sismember`](../sismember/), [`srem`](../srem/)
