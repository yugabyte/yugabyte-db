---
title: SMEMBERS
linkTitle: SMEMBERS
description: SMEMBERS
menu:
  v1.0:
    parent: api-redis
    weight: 2300
---
## Synopsis
<b>`SMEMBERS key`</b><br>
This command selects all members of the set that is associated with the given `key`.
<li>If `key` is associated with a value that is not a set, an error is raised.</li>
<li>If `key` does not exist, no value is returned.</li>

## Return Value
Returns all members of the given set.

## Examples

```sh
$ SADD yuga_world "Africa"
```

```
1
```

```sh
$ SADD yuga_world "America"
```

```
1
```

```sh
$ SMEMBERS yuga_world
```

```
1) "Africa"
2) "America"
```

## See Also
[`sadd`](../sadd/), [`scard`](../scard/), [`sismember`](../sismember/), [`srem`](../srem/)
