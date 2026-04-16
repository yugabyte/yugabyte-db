---
title: EXPIREAT
linkTitle: EXPIREAT
description: EXPIREAT
menu:
  preview:
    parent: api-yedis
    weight: 2062
aliases:
  - /preview/api/redis/expireat
  - /preview/api/yedis/expireat
type: docs
---

## Synopsis

**`EXPIREAT key ttl-as-timestamp`**

EXPIREAT has the same effect as EXPIRE, but instead of specifying the number of seconds representing the TTL (time to live), it takes an absolute Unix timestamp (seconds since January 1, 1970). A timestamp in the past will delete the key immediately.

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
$ EXPIREAT yugakey 1293840000
```

```
(integer) 1
```

```sh
$ EXISTS yugakey
```

```
(integer) 0
```

## See also

[`expire`](../expire/), [`ttl`](../ttl/), [`pttl`](../pttl/), [`set`](../set/)
