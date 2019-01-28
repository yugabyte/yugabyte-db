<img src="https://s3-us-west-2.amazonaws.com/assets.yugabyte.com/yb-db-logo.png" align="center" height="56" alt="YugaByte DB"/>

---------------------------------------

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Documentation Status](https://readthedocs.org/projects/ansicolortags/badge/?version=latest)](https://docs.yugabyte.com/)
[![Ask in forum](https://img.shields.io/badge/ask%20us-forum-orange.svg)](https://forum.yugabyte.com/)
[![Gitter chat](https://badges.gitter.im/gitlabhq/gitlabhq.svg)](https://gitter.im/YugaByte/Lobby)
[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/home?pixel&useReferer)](https://github.com/YugaByte/ga-beacon)

YugaByte Database is the open source, transactional, high-performance database for building internet-scale, globally-distributed applications.  This repository contains the Community Edition of the YugaByte Database.

## Table of Contents

- [About YugaByte DB](#about-yugabyte)
- [Architecture](#architecture)
- [Getting Started](#getting-started)
- [Developing Apps](#developing-apps)
- [Building YugaByte code](#building-yugabyte-code)
    - [Prerequisites for CentOS 7](#prerequisites-for-centos-7)
    - [Prerequisites for Mac OS X](#prerequisites-for-mac-os-x)
    - [Prerequisites for drivers and sample apps](#prerequisites-for-drivers-and-sample-apps)
    - [Building the code](#building-the-code)
    - [Running the C++ tests](#running-the-c-tests)
    - [Building Java code alone](#building-java-code-alone)
    - [Running the Java tests](#running-the-java-tests)
    - [Viewing log outputs of Java tests](#viewing-log-outputs-of-java-tests)
- [Reporting Issues](#reporting-issues)
- [Contributing](#contributing)
- [License](#license)

## About YugaByte DB

Built using a unique combination of log-structured merge document store, auto-sharding, per-shard distributed consensus replication and multi-shard ACID transactions (inspired by Google Spanner), YugaByte DB is world's only distributed database that is both non-relational (supports Redis-compatible key-value & Cassandra-compatible flexible schema APIs) and relational (PostgreSQL-compatible distributed SQL API) at the same time. It is purpose-built to power fast-growing online services on public, private and hybrid clouds with transactional integrity, low latency, high throughput and multi-region scalability while also providing unparalleled data modeling freedom to application architects. Enterprises gain more functional depth and agility without any cloud lock-in when compared to proprietary cloud databases such as Amazon DynamoDB, Microsoft Azure Cosmos DB and Google Cloud Spanner. Enterprises also benefit from stronger data integrity guarantees and higher performance than those offered by legacy open source NoSQL databases such as MongoDB and Apache Cassandra. 

* See how YugaByte DB [compares with other databases](https://docs.yugabyte.com/comparisons/).
* Read more about YugaByte DB in our [docs](https://docs.yugabyte.com/introduction/overview/).

## Architecture

YugaByte DB architecture has 2 layers. At the core is DocDB, YugaByte DB's distributed document store. DocDB is the common database engine for the YugaByte DB API layer. Applications interact with the YugaByte DB API layer using one or more of the APIs highlighted below.

### YugaByte DB APIs

YugaByte DB supports both Transactional NoSQL and Distributed SQL APIs.
* [YugaByte Dictionary Service (YEDIS)](https://docs.yugabyte.com/api/redis/) - A Redis-compatible Key-Value API with support for hash, sorted sets, pub/sub and time series data structures.
* [YugaByte Cloud Query Language (YCQL)](https://docs.yugabyte.com/api/cassandra/) - A Cassandra-compatible Flexible Schema API with strong consistency, distributed ACID transactions, globally consistent secondary indexes and a native JSONB data type.
* [YugaByte Structured Query Language (YSQL)](https://docs.yugabyte.com/api/postgresql/) - A PostgreSQL-compatible Distributed SQL API (currently in beta) with linear write scalability and extreme fault tolerance against infrastructure failures.

For transactional, internet-scale workloads, the question of which API to choose is a trade-off between data modeling richness and query performance. On one end of the spectrum is the YEDIS API that is completely optimized for single key access patterns, has simpler data modeling constructs and provides blazing-fast (sub-ms) query performance. On the other end of the spectrum is the YSQL API that supports complex multi-key relationships (through JOINS and foreign keys) and provides normal (single-digit ms) query performance. This is expected since multiple keys can be located on multiple shards hosted on multiple nodes, resulting in higher latency than a key-value API that accesses only a single key at any time. At the middle of the spectrum is the YCQL API that is still optimized for majority single-key workloads but has richer data modeling features such as globally consistent secondary indexes (powered by distributed ACID transactions) that can accelerate internet-scale application development significantly.

### DocDB, YugaByte DB's Distributed Document Store

[DocDB](https://docs.yugabyte.com/latest/architecture/concepts/persistence/) builds on top of the popular [RocksDB](https://rocksdb.org/) project by transforming RocksDB from a key-value store (with only primitive data types) to a document store (with complex data types). Every key is stored as a separate document in DocDB, irrespective of the API responsible for managing the key. DocDB’s sharding, replication/fault-tolerance and distributed ACID transactions architecture are all based on the the [Google Spanner](https://ai.google/research/pubs/pub39966) design first published in 2012.

## Getting Started

Here are a few resources for getting started with YugaByte DB:

* [Quick start guide](http://docs.yugabyte.com/quick-start/) - install, create a local cluster and read/write from YugaByte DB.
* [Explore core features](https://docs.yugabyte.com/explore/) - automatic sharding & re-balancing, linear scalability, fault tolerance, tunable reads etc.
* [Ecosystem integrations](https://docs.yugabyte.com/latest/develop/ecosystem-integrations/) - integrations with Apache Kafka/KSQL, Apache Spark, JanusGraph, KairosDB, Presto and more.
* [Real world apps](https://docs.yugabyte.com/develop/realworld-apps/) - sample real-world, end-to-end applications built using YugaByte DB.
* [Architecture docs](https://docs.yugabyte.com/architecture/) - to understand how YugaByte DB was designed and how it works

Cannot find what you are looking for? Have a question? We love to hear from you - please [file a GitHub issue](https://github.com/YugaByte/yugabyte-db/issues).

## Developing Apps

Here is a tutorial on implementing a simple Hello World application for YugaByte DB's YCQL and YEDIS APIs in different languages:
* [Java](https://docs.yugabyte.com/develop/client-drivers/java/) using Maven
* [NodeJS](https://docs.yugabyte.com/develop/client-drivers/nodejs/)
* [Python](https://docs.yugabyte.com/develop/client-drivers/python/)
* [Go](https://docs.yugabyte.com/latest/develop/client-drivers/go/)
* [C#](https://docs.yugabyte.com/latest/develop/client-drivers/csharp/)
* [C/C++](https://docs.yugabyte.com/latest/develop/client-drivers/cpp/)

We are constantly adding documentation on how to build apps using the client drivers in various
languages, as well as the ecosystem integrations we support. Please see [our app-development
docs](https://docs.yugabyte.com/develop/) for the latest information.

Once again, please post your questions or comments as a [GitHub issue
](https://github.com/YugaByte/yugabyte-db/issues) if you need something.

## Building YugaByte code

### Prerequisites for CentOS 7

CentOS 7 is the main recommended development and production platform for YugaByte.

Update packages on your system, install development tools and additional packages:

```bash
sudo yum update
sudo yum groupinstall -y 'Development Tools'
sudo yum install -y ruby perl-Digest epel-release ccache git python2-pip
sudo yum install -y cmake3 ctest3
```

Also we expect `cmake` / `ctest` binaries to be at least version 3. On CentOS one way to achive
this is to symlink them into `/usr/local/bin`.

```bash
sudo ln -s /usr/bin/cmake3 /usr/local/bin/cmake
sudo ln -s /usr/bin/ctest3 /usr/local/bin/ctest
```

You could also symlink them into another directory that is on your PATH.

We also use [Linuxbrew](https://github.com/linuxbrew/brew) to provide some of the third-party
dependencies on CentOS. During the build we install Linuxbrew in a separate directory,
`~/.linuxbrew-yb-build/linuxbrew-<version>`, so that it does not conflict with any other Linuxbrew
installation on your workstation, and does not contain any unnecessary packages that would
interfere with the build.

We don't need to add `~/.linuxbrew-yb-build/linuxbrew-<version>/bin` to PATH. The build scripts
will automatically discover this Linuxbrew installation.

### Prerequisites for Mac OS X

Install [Homebrew](https://brew.sh/):

```bash
/usr/bin/ruby -e "$(
  curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
```

Install the following packages using Homebrew:
```
brew install autoconf automake bash bison ccache cmake coreutils flex gnu-tar icu4c libtool maven \
             ninja pkg-config pstree wget zlib python@2
```

Also YugaByte DB build scripts rely on Bash 4. Make sure that `which bash` outputs
`/usr/local/bin/bash` before proceeding. You may need to put `/usr/local/bin` as the first directory
on PATH in your `~/.bashrc` to achieve that.

### Prerequisites for drivers and sample apps

YugaByte DB core is written in C++, but the repository contains Java code needed to run sample
applications. To build the Java part, you need:
* JDK 8
* [Apache Maven](https://maven.apache.org/).

Also make sure Maven's `bin` directory is added to your PATH, e.g. by adding to your `~/.bashrc`
```
export PATH=$HOME/tools/apache-maven-3.5.0/bin:$PATH
```
if you've installed Maven into `~/tools/apache-maven-3.5.0`.

For building YugaByte DB Java code, you'll need to install Java and Apache Maven.

**Java driver**

YugaByte DB and Apache Cassandra use different approaches to split data between nodes. In order to
route client requests to the right server without extra hops, we provide a [custom
load balancing policy](https://goo.gl/At7kvu) in [our modified version
](https://github.com/yugabyte/cassandra-java-driver) of Datastax's Apache Cassandra Java driver.

The latest version of our driver is available on Maven Central. You can build your application
using our driver by adding the following Maven dependency to your application:

```
<dependency>
  <groupId>com.yugabyte</groupId>
  <artifactId>cassandra-driver-core</artifactId>
  <version>3.2.0-yb-18</version>
</dependency>
```

### Building the code

Assuming this repository is checked out in `~/code/yugabyte-db`, do the following:

```
cd ~/code/yugabyte-db
./yb_build.sh release --with-assembly
```

The above command will build the release configuration, put the C++ binaries in
`build/release-gcc-dynamic-community`, and will also create the `build/latest` symlink to that
directory. Then it will build the Java code as well. The `--with-assembly` flag tells the build
script to build the `yb-sample-apps.jar` file containing sample Java apps.

For Linux it will first make sure our custom Linuxbrew distribution is installed into
`~/.linuxbrew-yb-build/linuxbrew-<version>`.

### Running the C++ tests

To run all the C++ tests you can use following command:
```
./yb_build.sh release --ctest
```

If you omit `release` argument, it will run java tests against debug YugaByte build.

To run specific test:

```
./yb_build.sh release --cxx-test util_monotime-test
```

Also you can run specific sub-test:

```
./yb_build.sh release --cxx-test util_monotime-test --gtest_filter TestMonoTime.TestCondition
```

### Building Java code alone

You can skip building C++ code, this can be useful when you only need to rebuild Java code:
```
cd ~/code/yugabyte-db
./yb_build.sh --scb
```

### Running the Java tests

Given that you've already built C++ and Java code you can run Java tests using following command:
```
./yb_build.sh release --scb --sj --java-tests
```

If you omit `release` argument, it will run java tests against debug YugaByte build, so you should then either
build debug binaries with `./yb_build.sh` or omit `--scb` and then it will build debug binaries automatically.

Alternatively, to run specific test:
```
./yb_build.sh release --scb --sj --java-test org.yb.client.TestYBClient
```

To run a specific Java sub-test within a test file use the # syntax, for example:
```
./yb_build.sh release --scb --sj --java-test org.yb.client.TestYBClient#testClientCreateDestroy
```

###  Viewing log outputs of Java tests

You can find Java tests output in corresponding directory (you might
need to change `yb-client` to respective Java tests module):
```
$ ls -1 java/yb-client/target/surefire-reports/
TEST-org.yb.client.TestYBClient.xml
org.yb.client.TestYBClient-output.txt
org.yb.client.TestYBClient.testAffinitizedLeaders.stderr.txt
org.yb.client.TestYBClient.testAffinitizedLeaders.stdout.txt
…
org.yb.client.TestYBClient.testWaitForLoadBalance.stderr.txt
org.yb.client.TestYBClient.testWaitForLoadBalance.stdout.txt
org.yb.client.TestYBClient.txt
```
Note that the YB logs are contained in the output file now.

## Reporting Issues

Please use [GitHub issues](https://github.com/YugaByte/yugabyte-db/issues) to report issues.

To live chat with engineers, use our [gitter channel](https://gitter.im/YugaByte/Lobby).

Also feel free to post questions on or subscribe to the [YugaByte Community Forum](http://forum.yugabyte.com).

## Contributing

We accept contributions as GitHub pull requests. Our code style is available
[here](https://goo.gl/Hkt5BU)
(mostly based on [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)).

## License

YugaByte DB Community Edition is distributed under an Apache 2.0 license. See the
[LICENSE.txt](https://github.com/YugaByte/yugabyte-db/blob/master/LICENSE.txt) file for
details.

