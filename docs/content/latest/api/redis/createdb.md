---
title: CREATEDB
linkTitle: CREATEDB
description: CREATEDB
menu:
  latest:
    parent: api-redis
    weight: 2032
aliases:
  - /latest/api/redis/createdb
  - /latest/api/yedis/createdb
---


## Synopsis

`CREATEDB` is used to create a new yedis database. All databases other than the default database ("0") need to be created before use.

A client can issue the `CREATEDB` command through the redis-cli.
This is required before issuing a `SELECT` command to start using the database.

## Return Value
Returns a status string, if creating the database was successful. Returns an error message upon error.

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

## See Also
[`createdb`](../createdb/)
[`listdb`](../listdb/)
[`deletedb`](../deletedb/)
[`flushdb`](../flushdb/)
[`flushall`](../flushall/)
[`select`](../select/)
