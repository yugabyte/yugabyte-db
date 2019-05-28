---
title: ysqlsh
linkTitle: ysqlsh
description: ysqlsh
aliases:
  - /develop/tools/ysqlsh/
  - /latest/develop/tools/ysqlsh/
menu:
  latest:
    identifier: ysqlsh
    parent: admin
    weight: 2459
isTocNested: true
showAsideToc: true
---

## Overview

`ysqlsh` is a command line shell for interacting with YugaByte DB through [YSQL](../../api/ysql/). It is derived from [`psql`](https://www.postgresql.org/docs/11/app-psql.html), the PostgreSQL shell.

## Download 

ysqlsh is installed as part of YugaByte DB and is located in the bin directory of YugaByte home. 

## Example

```sh
$ ./bin/ysqlsh
```

```
ysqlsh (11.2)
Type "help" for help.

postgres=#
```

## Defaults

ysqlsh defaults the following flags so that the user does not have to specify them.

```
-h 127.0.0.1 
-p 5433 
-U postgres
```
