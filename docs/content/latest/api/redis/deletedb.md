---
title: DELETEDB
linkTitle: DELETEDB
description: DELETEDB
menu:
  latest:
    parent: api-redis
    weight: 2034
aliases:
  - /latest/api/redis/deletedb
  - /latest/api/yedis/deletedb
isTocNested: true
showAsideToc: true
---

## Synopsis

`DELETEDB` is used to delete a yedis database that is no longer needed.

A client can issue the `DELETEDB` command through the redis-cli.

## Return Value
Returns a status string upon success.

## Examples
```{.sh .copy .separator-dollar}
$ LISTDB
```
```sh
1) "0"
```
```{.sh .copy .separator-dollar}
$ CREATEDB "second"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ CREATEDB "3.0"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ LISTDB
```
```sh
1) "0"
2) "3.0"
3) "second"
```
```{.sh .copy .separator-dollar}
$ DELETEDB "3.0"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ LISTDB
```
```sh
1) "0"
2) "second"
```

## See Also
[`createdb`](../createdb/)
[`listdb`](../listdb/)
[`deletedb`](../deletedb/)
[`flushdb`](../flushdb/)
[`flushall`](../flushall/)
[`select`](../select/)
