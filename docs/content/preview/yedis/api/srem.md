---
title: SREM
linkTitle: SREM
description: SREM
menu:
  preview:
    parent: api-yedis
    weight: 2310
aliases:
  - /preview/api/redis/srem
  - /preview/api/yedis/srem
type: docs
---

## Synopsis

**`SREM key value [value ...]`**

This command removes one or more specified members from the set that is associated with the given `key`.

- If the `key` does not exist, the associated set is an empty set, and the return value is zero.
- If the `key` is associated with a value that is not a set, an error is raised.
- If a specified `value` does not exist in the given set, that `value` is ignored and not counted toward the total of removed members.

## Return value

Returns the total number of existed members that were removed from the set.

## Examples

```sh
$ SADD yuga_world "America"
```

```
1
```

```sh
$ SADD yuga_world "Moon"
```

```
1
```

```sh
$ SREM yuga_world "Moon"
```

```
1
```

```sh
$ SREM yuga_world "Moon"
```

```
0
```

## See also

[`sadd`](../sadd/), [`scard`](../scard/), [`sismember`](../sismember/), [`smembers`](../smembers/)
