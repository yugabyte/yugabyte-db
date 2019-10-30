---
title: ysqlsh
linkTitle: ysqlsh
description: ysqlsh CLI for YSQL
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

The YSQL shell (`ysqlsh`) is a YugabyteDB command line interface (CLI), or shell, for using [YSQL](../../api/ysql/). The YSQL shell is derived from [`psql`](https://www.postgresql.org/docs/11/app-psql.html), the PostgreSQL shell.

## Download

The YSQL shell (`ysqlsh`) is installed with YugabyteDB and is located in the `bin` directory of YugabyteDB home.

## Example

```sh
$ ./bin/ysqlsh
```

```
ysqlsh (11.2-YB-2.0.0.0-b0)
Type "help" for help.

yugabyte=#
```

## Default options (flags)

When you open `ysqlsh`, the following flags are set so that the user does not have to specify them.

- Host: `-h 127.0.0.1`
- Port: `-p 5433`
- User: `-U yugabyte`

{{< note title="Note" >}}

Starting with v2.0.1, the default password for the default user `yugabyte` is `yugabyte`. If YSQL authentication is enabled, then the `yugabyte` user will be prompted for this password.

For v2.0.0 users, the default user `yugabyte` has no password. If you do not want any password to be prompted, then you should not enable YSQL authentication. If you want to enable YSQL authentication, then you must first set a password for the `yugabyte`  user (in a cluster with YSQL authentication turned off).

{{< /note >}}
