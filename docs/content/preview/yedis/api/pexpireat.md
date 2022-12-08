---
title: PEXPIREAT
linkTitle: PEXPIREAT
description: PEXPIREAT
menu:
  preview:
    parent: api-yedis
    weight: 2234
aliases:
  - /preview/api/redis/pexpireat
  - /preview/api/yedis/pexpireat
type: docs
---

## Synopsis

**`PEXPIREAT key ttl-as-timestamp`**

PEXPIREAT has the same effect as EXPIREAT, but the Unix timestamp at which the key will expire is specified in milliseconds instead of seconds.

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
$ PEXPIREAT yugakey 1555555555005
```

```
(integer) 1
```

```sh
$ PTTL yugakey
```

```
(integer) 18674452994
```

## See also

[`expireat`](../expireat/), [`ttl`](../ttl/), [`pttl`](../pttl/), [`set`](../set/)
