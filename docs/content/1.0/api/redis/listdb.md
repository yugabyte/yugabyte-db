---
title: LISTDB
linkTitle: LISTDB
description: LISTDB
menu:
  1.0:
    parent: api-redis
    weight: 2036
aliases:
  - api/redis/listdb
  - api/yedis/listdb
---

## Synopsis

`LISTDB` is used to list all the yedis database(s) available for use. All databases other than the default database ("0") need to be created using the `CREATEDB` command before use.

A client can issue the `LISTDB` command through the redis-cli.

## Return Value
Returns an array of string values, with the yedis database names. 

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
$ LISTDB
```
```sh
1) "0"
2) "second"
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
