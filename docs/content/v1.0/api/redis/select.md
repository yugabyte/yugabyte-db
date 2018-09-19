---
title: SELECT
linkTitle: "SELECT "
description: SELECT
menu:
  v1.0:
    parent: api-redis
    weight: 2038
aliases:
  - api/redis/select
  - api/yedis/select
---

## Synopsis

`SELECT` is used to change the yedis database that the client is communicating with. By default, all client connections start off communicating with the default database ("0"). To start using a database, other than the default database ("0"), it needs to be pre-created using the `CREATEDB` command before use.

## Return Value
Returns a status string if successful. Returns an error if the database is not already created.

## Examples
```{.sh .copy .separator-dollar}
$ SET k1 v1
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ GET k1
```
```sh
"v1"
```
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
$ SELECT "second"
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ SET k1 v2
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ GET k1
```
```sh
"v2"
```
```{.sh .copy .separator-dollar}
$ SELECT 0
```
```sh
"OK"
```
```{.sh .copy .separator-dollar}
$ GET k1
"v1"
```

## See Also
[`createdb`](../createdb/)
[`listdb`](../listdb/)
[`deletedb`](../deletedb/)
[`flushdb`](../flushdb/)
[`flushall`](../flushall/)
[`select`](../select/)
