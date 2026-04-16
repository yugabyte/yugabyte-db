---
title: EXPIRE
linkTitle: EXPIRE
description: EXPIRE
menu:
  preview:
    parent: api-yedis
    weight: 2061
aliases:
  - /preview/api/redis/expire
  - /preview/api/yedis/expire
type: docs
---

## Synopsis

**`EXPIRE key timeout`**

Set a timeout on key (in seconds). After the timeout has expired, the key will automatically be deleted.

## Return value

Returns integer reply, specifically 1 if the timeout was set and 0 if key does not exist.

## Examples

```sh
$ SET yugakey "Yugabyte"
```

```
"OK"
```

```sh
$ EXPIRE yugakey 10
```

```
(integer) 1
```

```sh
$ EXPIRE non-existent-key 10
```

```
(integer) 0
```

## See also

[`expireat`](../expireat/), [`ttl`](../ttl/), [`pttl`](../pttl/), [`set`](../set/)
