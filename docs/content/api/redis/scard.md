---
title: SCARD
weight: 2260
---

## SYNOPSIS
<b>`SCARD key`</b><br>
This command finds the cardinality of the set that is associated with the given `key`. Cardinality is the number of elements in a set.
<li>If the `key` does not exist, 0 is returned.</li>
<li>If the `key` is associated with a non-set value, an error is raised.</li>

## RETURN VALUE
Returns the cardinality of the set.

## EXAMPLES
```{.sh .copy .separator-dollar}
$ SADD yuga_world "America"
```
```sh
1
```
```{.sh .copy .separator-dollar}
$ SADD yuga_world "Asia"
```
```sh
1
```
```{.sh .copy .separator-dollar}
$ SCARD yuga_world
```
```sh
2
```

## SEE ALSO
[`sadd`](../sadd/), [`sismember`](../sismember/), [`smembers`](../smembers/), [`srem`](../srem/)
