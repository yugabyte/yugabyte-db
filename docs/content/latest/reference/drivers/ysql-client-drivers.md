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

The [libqxx](http://pqxx.org/development/libpqxx/) is the official C++ client API for PostgreSQL.

For details and documentation, see the [`jvt/libpqxx` README](https://github.com/jtv/libpqxx#readme) and the [`libpqxx` ReadTheDocs](https://libpqxx.readthedocs.io/en/latest/).

For a tutorial on building a sample C++ application with this driver, see [Build a C++ application](../../quick-start/build-apps/cpp/ysql/).

#### Install the libpqxx driver

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

The `libpqxx` driver is ready for use building a C++ application for YugabyteDB.

## C\#

### Npgsql

[Npgsql](https://www.npgsql.org/) is an open source ADO.NET Data Provider for PostgreSQL that enables C# applications to connect and interact with PostgreSQL databases.

For details on installing and using Npgsql, see [Npgsql documentation](https://www.npgsql.org/doc/).

To follow a tutorial on building a sample C# application with this driver, see [Build a C++ application](../../quick-start/build-apps/csharp/ysql/).

#### Install the driver

To install Npgsql in your Visual Studio project, follow the steps below.

1. Open the **Project Solution View**.

2. Right-click on **Packages** and click **Add Packages**.

![Add Package](/images/develop/client-drivers/csharp/visual-studio-add-package.png)

3. Search for `Npgsql` and click **Add Package**.

The `Npgsql` driver is ready for building C# applications that use YugabyteDB.

## Go

### Go PostgreSQL driver (pq)

The [Go PostgreSQL driver package (`pq`)](https://pkg.go.dev/github.com/lib/pq?tab=doc) is a Go PostgreSQL driver for the `database/sql` package.

For a tutorial on building a sample Go application with this driver, see [Build a Go application](../../quick-start/build-apps/go/ysql/) and click **YSQL-PQ**.

### Install the pq driver

To install the package locally, run the following [`go get`](https://golang.org/cmd/go/#hdr-Add_dependencies_to_current_module_and_install_them) command:

```sh
$ go get github.com/lib/pq
```

The `pq` driver is ready for building Go applications that use YugabyteDB.

## Java

### PostgreSQL JDBC Driver (PgJDBC)

The [PostgreSQL JDBC driver](https://jdbc.postgresql.org/) is the official PostgreSQL driver.

For a tutorial on building a sample Go application with this driver, see [Build a Java application](../../quick-start/build-apps/java/ysql/) and click **YSQL-JDBC**.

### Install the PostgreSQL JDBC Driver

To download binary JAR files, go to the [PostgreSQL JDBC Driver download page](https://jdbc.postgresql.org/download.html).  Because Java is platform neutral, download the appropriate JAR file and drop it into the classpath.

For the latest versions for projects using [Apache Maven](https://maven.apache.org), see [Maven Central Repository Search](https://search.maven.org/artifact/org.postgresql/postgresql/42.2.14.jre7/jar).

### YugabyteDB JDBC driver [BETA]

The [YugabyteDB JDBC driver](https://jdbc.postgresql.org/) is based on the [PostgreSQL JDBC Driver (PgJDBC)](#postgresql-jdbc-driver) and incorporates all of the functionality and behavior of that driver. The YugabyteDB JDBC driver extends PgJDBC and adds support for distributed SQL databases created in YugabyteDB universes, including cluster awareness, connection pooling, and load balancing.

For details, see the [YugabyteDB JDBC Driver](../yugabytedb-jdbc-driver) page.

## Node.JS

### node-postgres

[`node-postgres`](https://node-postgres.com/) is a collection of Node.js modules for interacting with PostgreSQL databases.

For details on installing and using node-postgres, see the [node-postgres documentation].

For a tutorial on building a sample Node.js application that uses `node-postgres`, see [Build a Node.js application](../../quick-start/build-apps/nodejs/ysql-pg/).

#### Install the node-postgres (pg) driver

To install the `node-postgres` and any packages it depends on, run the following [`npm install`](https://docs.npmjs.com/cli/install.html) command:

```sh
$ npm install pg
```

## PHP

### php-pgsql

`php-pgsql`

For a tutorial on building a sample Node.js application that uses `php-pgsql`, see [Build a Node.js application](../../quick-start/build-apps/php/ysql/).

#### Install the php-pgsql driver

To enable PostgreSQL support using `php-pgsql`, see [Installing/Configuring] in the PHP documentation.

## Python

### psycopg

[Psycopg](https://www.psycopg.org/) is the popular PostgreSQL database adapter for the Python programming language and is a wrapper for `libpq`, the official PostgreSQL client library.

For details on using psycopg, see [Psycopg documentation](https://www.psycopg.org/docs/).

For a tutorial on building a sample Python application that uses `psycopg2`, see [Build a Python application](../../quick-start/build-apps/python/ysql/).

#### Install psycopg2

To install the `psycopg2` binary package, run the following pip install` command:

```sh
$ pip3 install psycopg2
```

## Ruby

### Ruby PostgreSQL Driver (pg)

The Ruby PostgreSQL Driver (`pg`) is the Ruby interface for PostgreSQL databases and supports the functions defined in the libpq C library. 

https://docs.yugabyte.com/latest/quick-start/build-apps/ruby/ysql-pg/

### Install the pg driver

To install `pg` driver, run the following [`gem install`](https://guides.rubygems.org/command-reference/#gem-install) command:

```sh
$ gem install pg -- --with-pg-config=<yugabyte-install-dir>/postgres/bin/pg_config



```
