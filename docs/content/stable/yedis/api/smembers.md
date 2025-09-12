---
title: SMEMBERS
linkTitle: SMEMBERS
description: SMEMBERS
menu:
  preview:
    parent: api-yedis
    weight: 2300
aliases:
  - /preview/api/redis/smembers
  - /preview/api/yedis/smembers
type: docs
---

## Synopsis

**`SMEMBERS key`**

This command selects all members of the set that is associated with the given `key`.

- If `key` is associated with a value that is not a set, an error is raised.
- If `key` does not exist, no value is returned.

## Return value

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

## See also

[`sadd`](../sadd/), [`scard`](../scard/), [`sismember`](../sismember/), [`srem`](../srem/)
