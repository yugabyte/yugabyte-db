---
title: PTTL
linkTitle: PTTL
description: PTTL
menu:
  v1.1:
    parent: api-redis
    weight: 2235
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`PTTL key`</b><br>
Similar to TTL this command returns the remaining time to live of a key that has a timeout set, with the sole difference that TTL returns the amount of remaining time in seconds while PTTL returns it in milliseconds.

## Return Value
Returns TTL in milliseconds, encoded as integer response.

## Examples

```sh
$ SET yugakey "YugaByte"
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
$ PTTL yugakey
```

```
(integer) 9995
```

## See Also
[`ttl`](../ttl/), [`set`](../set/), [`expire`](../expire/), [`expireat`](../expireat/)
