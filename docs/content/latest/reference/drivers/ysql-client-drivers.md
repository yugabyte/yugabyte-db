---
title: API client drivers for YSQL
headerTitle: API client drivers for YSQL
linkTitle: API client drivers for YSQL
description: Lists the API client library drivers that you can use to build and access YSQL applications. 
menu:
  latest:
    identifier: ysql-client-libraries
    parent: drivers
    weight: 2942
isTocNested: true
showAsideToc: true
---

The following API client library drivers are supported for use with the [Yugabyte Structured Query Language (YSQL) API](../../../api/ysql/).

For tutorials on building a sample application with the following API client drivers, click the relevant link included below for each driver.

## C/C++

### libpqxx

The [libqxx](https://github.com/jtv/libpqxx) is the official C++ client API for PostgreSQL.

For details, see the [README](https://github.com/jtv/libpqxx/blob/master/README.md).

For a tutorial on building a sample C++ application with this driver, see [Build a C++ application](../../quick-start/build-apps/cpp/ysql/).

#### Install the driver

To build and install the libpqxx driver for use with YugabyteDB, follow these steps:

1. Clone the libpqxx repository.

```sh
$ git clone https://github.com/jtv/libpqxx.git
```

2. For dependencies on the PostgreSQL binaries, add the PostgreSQL `bin` directory to the command path.

```sh
$ export PATH=$PATH:<yugabyte-install-dir>/postgres/bin
```

3. Build and install the driver.

```sh
$ cd libpqxx
$ ./configure
$ make
$ make install
```

The driver is ready for use building a C++ application for YugabyteDB.

## C\#

### Npgsql

[Npgsql](https://www.npgsql.org/) is an open source ADO.NET Data Provider for PostgreSQL that enables C# applications to connect and interact with PostgreSQL databases. 

For documentation on installing and using Npgsql, see [Npgsql documentation](https://www.npgsql.org/doc/).

To follow a tutorial on building a sample C# application with this driver, see [Build a C++ application](../../quick-start/build-apps/csharp/ysql/).

#### Install the driver