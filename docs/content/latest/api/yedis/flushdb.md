---
title: FLUSHDB
linkTitle: FLUSHDB
description: FLUSHDB
menu:
  latest:
    parent: api-redis
    weight: 2065
aliases:
  - /latest/api/redis/flushdb
  - /latest/api/yedis/flushdb
isTocNested: true
showAsideToc: true
---

## Synopsis
<b>`FLUSHDB`</b><br>
This command deletes all keys from a database.

This functionality can be disabled by setting the yb-tserver gflag `yedis_enable_flush` to `false`.

## Return Value
Returns status string.

## Examples

```sh
$ SET yuga1 "America"
```

```
"OK"
```

```sh
$ SET yuga2 "Africa"
```

```
"OK"
```

```sh
$ GET yuga1
```

```
"America"
```

```sh
$ GET yuga2
```

```
"Africa"
```

```sh
$ FLUSHDB
```

```
"OK"
```

```sh
$ GET yuga1
```

```
(null)
```

```sh
$ GET yuga2
```

```
(null)
```

## See Also
[`del`](../del/), [`flushall`](../flushall/),[`deletedb`](../deletedb/)
