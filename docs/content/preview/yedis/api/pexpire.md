---
title: PEXPIRE
linkTitle: PEXPIRE
description: PEXPIRE
menu:
  preview:
    parent: api-yedis
    weight: 2233
aliases:
  - /preview/api/redis/pexpire
  - /preview/api/yedis/pexpire
type: docs
---

## Synopsis

**`PEXPIRE key timeout`**

This command works exactly like EXPIRE but the time to live of the key is specified in milliseconds instead of seconds.

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
$ PEXPIRE yugakey 10000
```

```
(integer) 1
```

```sh
$ PTTL yugakey
```

```
(integer) 9995
```

## See also

[`expire`](../expire/), [`ttl`](../ttl/), [`pttl`](../pttl/), [`set`](../set/)
