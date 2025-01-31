---
title: Client drivers for YSQL
headerTitle: Client drivers for YSQL
linkTitle: Client drivers for YSQL
description: Lists the client drivers that you can use to connect to and interact with the YSQL API.
menu:
  v2.14:
    identifier: ysql-client-libraries
    parent: drivers
    weight: 2940
type: docs
---

The [Yugabyte Structured Query Language (YSQL) API](../../../api/ysql/) builds upon and extends a fork of the query layer from PostgreSQL 11.2, with the intent of supporting most PostgreSQL functionality and adding new functionality to supported distributed SQL databases.

For details on PostgreSQL feature support in YSQL, refer to [SQL feature support](../../../explore/ysql-language-features/sql-feature-support/).

Yugabyte and third party client drivers listed below are supported for developing applications that connect to and interact with the YSQL API. Most of the drivers use [libpq](#libpq) and support the [SCRAM-SHA-256 authentication method](../../../secure/authentication/password-authentication/#scram-sha-256).

For help using these drivers with YSQL, ask your questions in the [Slack community]({{<slack-invite>}}).

If you encounter an issue or have an enhancement request, [file a GitHub issue](https://github.com/yugabyte/yugabyte-db/issues/new/choose).

## C

### libpq

[libpq](https://www.postgresql.org/docs/11/libpq.html) is the C client library for connecting to and interacting with PostgreSQL databases. libpq is also the underlying engine used in other PostgreSQL application interfaces. The libpq client library supports the [SCRAM-SHA-256 authentication method](../../../secure/authentication/password-authentication/#scram-sha-256).

For details and documentation, refer to [libpq - C Library](https://www.postgresql.org/docs/11/libpq.html) for PostgreSQL 11 (on which YugabyteDB is based).

For a tutorial on building a sample C application with libpq, refer to [Build a C application](../../../quick-start/build-apps/c/ysql/).

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

For a tutorial on building a sample C++ application with libpqxx, refer to [Build a C++ application](../../../quick-start/build-apps/cpp/ysql/).

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

## C\#

### Npgsql

[Npgsql](https://www.npgsql.org/) is an open source ADO.NET Data Provider for PostgreSQL that enables C# applications to connect and interact with PostgreSQL databases. Npgsql is based on [libpq](#libpq) and supports the [SCRAM-SHA-256 authentication method](../../../secure/authentication/password-authentication/#scram-sha-256).

For details on Npgsql, refer to the [Npgsql documentation](https://www.npgsql.org/doc/).

To follow a tutorial on building a sample C# application with Npgsql, see [Build a C# application](../../../quick-start/build-apps/csharp/ysql/).

#### Install the driver

To include Npgsql in your application, add the following package reference to your `.cproj` file.

```cpp
<PackageReference Include="npgsql" Version="6.0.3" />
```

If you are using Visual Studio, add Npgsql to your project as follows:

1. Open the **Project Solution View**.

2. Right-click on **Packages** and click **Add Packages**.

3. Search for Npgsql and click **Add Package**.

{{< warning title="Warning" >}}

On every new connection the NpgSQL driver also makes [extra system table queries to map types](https://github.com/npgsql/npgsql/issues/1486), which adds significant overhead. To turn off this behavior, set the following option in your connection string builder:

```csharp
connStringBuilder.ServerCompatibilityMode = ServerCompatibilityMode.NoTypeLoading;
```

{{< /warning >}}

## Go

### Go PostgreSQL driver (pq)

The [Go PostgreSQL driver package (pq)](https://pkg.go.dev/github.com/lib/pq?tab=doc) is a Go PostgreSQL driver for the `database/sql` package. pq is not based on [libpq](#libpq), but supports the [SCRAM-SHA-256 authentication method](../../../secure/authentication/password-authentication/#scram-sha-256).

For a tutorial on building a sample Go application with pq, see [Build a Go application](../../../quick-start/build-apps/go/ysql-pq/).

#### Install the pq driver

To install the package locally, run the following [`go get`](https://golang.org/cmd/go/#hdr-Add_dependencies_to_current_module_and_install_them) command:

```sh
$ go get github.com/lib/pq
```

The pq driver is ready for building Go applications that connect to and interact with YugabyteDB.

## Java

### YugabyteDB JDBC Smart Driver

The YugabyteDB JDBC Smart Driver is a distributed JDBC Smart Driver for YSQL built on the PostgreSQL JDBC driver, with features that eliminate the need for external load balancers.

For information on the YugabyteDB JDBC Smart Driver and its load balancing features, see [YugabyteDB JDBC Smart Driver](../../../reference/drivers/java/yugabyte-jdbc-reference/).

For a tutorial on building a sample Java application with the YugabyteDB JDBC driver, see [Connect an application](../../../drivers-orms/java/yugabyte-jdbc/).

### PostgreSQL JDBC driver (PgJDBC)

The [PostgreSQL JDBC driver](https://jdbc.postgresql.org/) is the official JDBC driver for PostgreSQL. PgJDBC is not based on [libpq](#libpq), but supports the [SCRAM-SHA-256 authentication method](../../../secure/authentication/password-authentication/#scram-sha-256).

For a tutorial on building a sample Java application with the PostgreSQL JDBC Driver, see [Build a Java application](../../../quick-start/build-apps/java/ysql-jdbc/).

#### Install the PostgreSQL JDBC driver

To download binary JAR files, go to [PostgreSQL JDBC driver – Downloads](https://jdbc.postgresql.org/download/). Because Java is platform neutral, download the appropriate JAR file and drop it into the classpath.

To get the latest versions for projects using [Apache Maven](https://maven.apache.org), see [Maven Central Repository Search](https://search.maven.org/artifact/org.postgresql/postgresql/42.2.14.jre7/jar).

## Node.JS

### node-postgres

[node-postgres](https://node-postgres.com/) is a collection of Node.js modules for interacting with PostgreSQL databases. node-postgres optionally uses [libpq](#libpq) and supports the [SCRAM-SHA-256 authentication method](../../../secure/authentication/password-authentication/#scram-sha-256).

For details on installing and using node-postgres, see the [node-postgres documentation](https://node-postgres.com/).

For a tutorial on building a sample Node.js application with node-postgres, see [Build a Node.js application](../../../quick-start/build-apps/nodejs/ysql-pg/).

#### Install the node-postgres (pg) driver

To install node-postgres and any packages it depends on, run the following [`npm install`](https://docs.npmjs.com/cli/install.html) command:

```sh
$ npm install pg
```

## PHP

### php-pgsql

The [php-pgsql](https://www.php.net/manual/en/book.pgsql.php) driver is a collection of the official PostgreSQL module for PHP. php-pgsql is based on [libpq](#libpq) and supports the [SCRAM-SHA-256 authentication method](../../../secure/authentication/password-authentication/#scram-sha-256).

For details on installing and using php-pgsql, see the [php-pgsql documentation](https://www.php.net/manual/en/book.pgsql.php).

For a tutorial on building a sample PHP application with php-pgsql, see [Build a PHP application](../../../quick-start/php/ysql/).

#### Install the php-pgsql driver

To enable PostgreSQL support using php-pgsql, see [Installing/Configuring](https://www.php.net/manual/en/pgsql.setup.php) in the PHP documentation.

Homebrew users on macOS can install PHP using `brew install php`; the php-pgsql driver is installed automatically.

Ubuntu users can install the driver using the `sudo apt-get install php-pgsql` command.

CentOS users can install the driver using the `sudo yum install php-pgsql` command.

## Python

### psycopg2

[Psycopg](https://www.psycopg.org/) is the popular PostgreSQL database adapter for the Python programming language. psycopg2 is based on [libpq](#libpq) and supports the [SCRAM-SHA-256 authentication method](../../../secure/authentication/password-authentication/#scram-sha-256).

For details on using psycopg2, see [Psycopg documentation](https://www.psycopg.org/docs/).

For a tutorial on building a sample Python application that uses psycopg2, see [Build a Python application](../../../quick-start/build-apps/python/ysql-psycopg2).

#### Install the psycopg2 binary

To install the psycopg2 binary package, run the following `pip3 install` command:

```sh
$ pip3 install psycopg2-binary
```

### aiopg

[aiopg](https://aiopg.readthedocs.io/en/stable/) is a library for accessing a PostgreSQL database using the asyncio (PEP-3156/tulip) framework. It wraps asynchronous features of the [Psycopg](https://www.psycopg.org/) database driver. For details on using aiopg, see [aiopg documentation](https://aiopg.readthedocs.io/en/stable/).

For a tutorial on building a sample Python application that uses aiopg, see [Build a Python application](../../../quick-start/build-apps/python/ysql-aiopg).

#### Install

To install the aiopg package, run the following `pip3 install` command:

```sh
pip3 install aiopg
```

## Ruby

### pg

[pg](https://github.com/ged/ruby-pg) is the Ruby interface for PostgreSQL databases. pg is based on [libpq](#libpq) and supports the [SCRAM-SHA-256 authentication method](../../../secure/authentication/password-authentication/#scram-sha-256).

For a tutorial on building a sample Ruby application with pg, see [Build a Ruby application](../../../quick-start/build-apps/ruby/ysql-pg).

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

For a tutorial on building a sample Ruby application with Rust-Postgres, see [Build a Rust application](../../../yugabyte-cloud/cloud-quickstart/cloud-build-apps/cloud-ysql-rust).

#### Install the Rust-Postgres driver

To include the Rust-Postgres driver with your application, add the following dependencies to your `cargo.toml` file:

```sh
postgres = "0.19.2"
openssl = "0.10.38"
postgres-openssl = "0.5.0"
```
