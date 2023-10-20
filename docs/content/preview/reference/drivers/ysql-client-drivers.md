---
title: Client drivers for YSQL
headerTitle: Additional client drivers for YSQL
linkTitle: Client drivers for YSQL
description: Lists the client drivers that you can use to connect to and interact with the YSQL API.
menu:
  preview:
    identifier: ysql-client-libraries
    parent: drivers
    weight: 2940
type: docs
rightNav:
  hideH4: true
---

The [Yugabyte Structured Query Language (YSQL) API](../../../api/ysql/) builds upon and extends a fork of the query layer from PostgreSQL 11.2, with the intent of supporting most PostgreSQL functionality and adding new functionality to supported distributed SQL databases.

For details on PostgreSQL feature support in YSQL, refer to [SQL feature support](../../../explore/ysql-language-features/sql-feature-support/).

Client drivers listed below are supported for developing applications that connect to and interact with the YSQL API. Most of the drivers use [libpq](#libpq) and support the [SCRAM-SHA-256 authentication method](../../../secure/authentication/password-authentication/#scram-sha-256).

For help using these drivers with YSQL, ask your questions in the [Slack community]({{<slack-invite>}}).

If you encounter an issue or have an enhancement request, [file a GitHub issue](https://github.com/yugabyte/yugabyte-db/issues/new/choose).

## C

### libpq

[libpq](https://www.postgresql.org/docs/11/libpq.html) is the C client library for connecting to and interacting with PostgreSQL databases. libpq is also the underlying engine used in other PostgreSQL application interfaces. The libpq client library supports the [SCRAM-SHA-256 authentication method](../../../secure/authentication/password-authentication/#scram-sha-256).

For details and documentation, refer to [libpq - C Library](https://www.postgresql.org/docs/11/libpq.html) for PostgreSQL 11 (on which YugabyteDB is based).

For a tutorial on building a sample C application with libpq, refer to [Connect an application](../../../drivers-orms/c/ysql/).

#### Install the libpq client library

The libpq C driver is included in the YugabyteDB installation. You can use it by setting the `LD_LIBRARY_PATH` as follows:

```sh
$ export LD_LIBRARY_PATH=<yugabyte-install-dir>/postgres/lib
```

Homebrew users on macOS can install libpq using `brew install libpq`. You can download the PostgreSQL binaries and source from the [PostgreSQL Downloads](https://www.postgresql.org/download/) page.

## C++

### libpqxx

The [libpqxx](http://pqxx.org/development/libpqxx/) driver is the official C++ client API for PostgreSQL. libpqxx is based on [libpq](#libpq) and supports the [SCRAM-SHA-256 authentication method](../../../secure/authentication/password-authentication/#scram-sha-256).

For details and documentation, refer to the [libpqxx README](https://github.com/jtv/libpqxx#readme) and [libpqxx documentation](https://libpqxx.readthedocs.io/en/latest/).

For a tutorial on building a sample C++ application with libpqxx, refer to [Connect an application](../../../drivers-orms/cpp/ysql/).

#### Install the libpqxx driver

To build and install the libpqxx driver for use with YugabyteDB, first Clone the libpqxx repository.

```sh
$ git clone https://github.com/jtv/libpqxx.git
```

For dependencies on the PostgreSQL binaries, add the PostgreSQL `bin` directory to the command path by running the following command.

```sh
$ export PATH=$PATH:<yugabyte-install-dir>/postgres/bin
```

Build and install the driver.

```sh
$ cd libpqxx
$ ./configure
$ make
$ make install
```

## Java

### Vert.x PG Client

[Vert.x PG Client](https://vertx.io/docs/vertx-pg-client/java/) is the client for PostgreSQL with basic APIs to communicate with the database. It is a reactive and non-blocking client for handling the database connections with a single threaded API.

For a tutorial on building a sample Java application with the Vert.x PG Client, see [Connect an application](../../../drivers-orms/java/ysql-vertx-pg-client/).

To get the latest versions for projects using [Apache Maven](https://maven.apache.org), see [Maven Central Repository of Vert.x PG Client](https://mvnrepository.com/artifact/io.vertx/vertx-pg-client).

## PHP

### php-pgsql

The [php-pgsql](https://www.php.net/manual/en/book.pgsql.php) driver is a collection of the official PostgreSQL module for PHP. php-pgsql is based on [libpq](#libpq) and supports the [SCRAM-SHA-256 authentication method](../../../secure/authentication/password-authentication/#scram-sha-256).

For details on installing and using php-pgsql, see the [php-pgsql documentation](https://www.php.net/manual/en/book.pgsql.php).

For a tutorial on building a sample PHP application with php-pgsql, see [Connect an application](../../../drivers-orms/php/ysql/).

#### Install the php-pgsql driver

To enable PostgreSQL support using php-pgsql, see [Installing/Configuring](https://www.php.net/manual/en/pgsql.setup.php) in the PHP documentation.

Homebrew users on macOS can install PHP using `brew install php`; the php-pgsql driver is installed automatically.

Ubuntu users can install the driver using the `sudo apt-get install php-pgsql` command.

CentOS users can install the driver using the `sudo yum install php-pgsql` command.

## Python

### aiopg

[aiopg](https://aiopg.readthedocs.io/en/stable/) is a library for accessing a PostgreSQL database using the asyncio (PEP-3156/tulip) framework. It wraps asynchronous features of the [Psycopg](https://www.psycopg.org/) database driver. For details on using aiopg, see [aiopg documentation](https://aiopg.readthedocs.io/en/stable/).

For a tutorial on building a sample Python application that uses aiopg, see [YSQL Aiopg](../../../drivers-orms/python/aiopg/).

#### Install

To install the aiopg package, run the following `pip3 install` command:

```sh
pip3 install aiopg
```

## Ruby

### pg

[pg](https://github.com/ged/ruby-pg) is the Ruby interface for PostgreSQL databases. pg is based on [libpq](#libpq) and supports the [SCRAM-SHA-256 authentication method](../../../secure/authentication/password-authentication/#scram-sha-256).

For a tutorial on building a sample Ruby application with pg, see [Connect an application](../../../drivers-orms/ruby/ysql-pg/).

#### Install the pg driver

If you have installed YugabyteDB locally, run the following [gem install](https://guides.rubygems.org/command-reference/#gem-install) command to install the pg driver:

```sh
$ gem install pg -- --with-pg-config=<yugabyte-install-dir>/postgres/bin/pg_config
```

Otherwise, to install pg, run the following command:

```sh
gem install pg -- --with-pg-include=<path-to-libpq>/libpq/include --with-pg-lib=<path-to-libpq>/libpq/lib
```

Replace `<path-to-libpq>` with the path to the libpq installation; for example, `/usr/local/opt`.

## Rust

### Rust-Postgres

[Rust-Postgres](https://github.com/sfackler/rust-postgres) is the Rust interface for PostgreSQL databases. Rust-Postgres is not based on libpq, but supports the [SCRAM-SHA-256 authentication method](../../../secure/authentication/password-authentication/#scram-sha-256).

For a tutorial on building a sample Ruby application with Rust-Postgres, see [Build a Rust application](../../../develop/build-apps/rust/cloud-ysql-rust/).

#### Install the Rust-Postgres driver

To include the Rust-Postgres driver with your application, add the following dependencies to your `cargo.toml` file:

```sh
postgres = "0.19.2"
openssl = "0.10.38"
postgres-openssl = "0.5.0"
```
