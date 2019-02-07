---
title: SELECT
linkTitle: "SELECT "
description: SELECT
menu:
  v1.0:
    parent: api-redis
    weight: 2038
---

## Synopsis

`SELECT` is used to change the yedis database that the client is communicating with. By default, all client connections start off communicating with the default database ("0"). To start using a database, other than the default database ("0"), it needs to be pre-created using the `CREATEDB` command before use.

## Return Value
Returns a status string if successful. Returns an error if the database is not already created.

## Examples

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ SET k1 v1
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ GET k1
```
</div>
```sh
"v1"
```
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
$ SELECT "second"
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ SET k1 v2
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ GET k1
```
</div>
```sh
"v2"
```
<div class='copy separator-dollar'>
```sh
$ SELECT 0
```
</div>
```sh
"OK"
```
<div class='copy separator-dollar'>
```sh
$ GET k1
"v1"
```
</div>

## See Also
[`createdb`](../createdb/)
[`listdb`](../listdb/)
[`deletedb`](../deletedb/)
[`flushdb`](../flushdb/)
[`flushall`](../flushall/)
[`select`](../select/)
