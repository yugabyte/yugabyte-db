---
title: ZCARD
weight: 2380
---

## SYNOPSIS
<b>`ZCARD key`</b><br>
This command returns the number of `members` in the sorted set at `key`. If `key` does not exist, 0 is returned.
If `key` is associated with non sorted-set data, an error is returned.

## RETURN VALUE

The cardinality of the sorted set.

## EXAMPLES
```{.sh .copy .separator-dollar}
$ ZADD z_key 1.0 v1 2.0 v2
```
```sh
(integer) 2
```
```{.sh .copy .separator-dollar}
$ ZADD z_key 3.0 v2
```
```sh
(integer) 0
```
```{.sh .copy .separator-dollar}
$ ZCARD z_key 
```
```sh
(integer) 2
```
```{.sh .copy .separator-dollar}
$ ZCARD ts_key 
```
```sh
(integer) 0
```
## SEE ALSO
[`zadd`](../zadd/), [`zrangebyscore`](../zrangebyscore/), [`zrem`](../zrem/)
