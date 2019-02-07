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
isTocNested: true
showAsideToc: true
---


## Synopsis

`CREATEDB` is used to create a new yedis database. All databases other than the default database ("0") need to be created before use.

A client can issue the `CREATEDB` command through the redis-cli.
This is required before issuing a `SELECT` command to start using the database.

## Return Value
Returns a status string, if creating the database was successful. Returns an error message upon error.

## Examples

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ LISTDB
```
</div>
```sh
1) "0"
```
<div class='copy separator-dollar'>
```sh
$ CREATEDB "second"
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ LISTDB
```
</div>
```sh
1) "0"
2) "second"
```
<div class='copy separator-dollar'>
```sh
$ CREATEDB "3.0"
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ LISTDB
```
</div>
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
