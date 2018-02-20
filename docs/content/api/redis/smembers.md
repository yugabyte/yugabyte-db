---
title: SMEMBERS
weight: 2300
---
## SYNOPSIS
<b>`SMEMBERS key`</b><br>
This command selects all members of the set that is associated with the given `key`.
<li>If `key` is associated with a value that is not a set, an error is raised.</li>
<li>If `key` does not exist, no value is returned.</li>

## RETURN VALUE
Returns all members of the given set.

## EXAMPLES
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

## SEE ALSO
[`sadd`](../sadd/), [`scard`](../scard/), [`sismember`](../sismember/), [`srem`](../srem/)
