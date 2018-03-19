<img src="https://www.yugabyte.com/images/yblogo_whitebg.3fea4ef9.png" align="center" height="56" alt="YugaByte DB"/>

---------------------------------------

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Documentation Status](https://readthedocs.org/projects/ansicolortags/badge/?version=latest)](https://docs.yugabyte.com/)
[![Ask in forum](https://img.shields.io/badge/ask%20us-forum-orange.svg)](https://forum.yugabyte.com/)
[![Gitter chat](https://badges.gitter.im/gitlabhq/gitlabhq.svg)](https://gitter.im/YugaByte/Lobby)

A cloud-native database for building mission-critical applications. This repository contains the Community Edition of the YugaByte Database.

## Table of Contents

- [About YugaByte](#about-yugabyte)
- [Supported APIs](#supported-apis)
- [Getting Started](#getting-started)
- [Developing Apps](#developing-apps)
- [Building YugaByte code](#building-yugabyte-code)
    - [Prerequisites for CentOS 7](#prerequisites-for-centos-7)
    - [Prerequisites for Mac OS X](#prerequisites-for-mac-os-x)
    - [Prerequisites for drivers and sample apps](#prerequisites-for-drivers-and-sample-apps)
    - [Building the code](#building-the-code)
- [Reporting Issues](#reporting-issues)
- [Contributing](#contributing)
- [License](#license)

## About YugaByte

YugaByte offers **both** SQL and NoSQL in a single, unified db. It is meant to be a system-of-record/authoritative database that applications can rely on for correctness and availability. It allows applications to easily scale up and scale down in the cloud, on-premises or across hybrid environments without creating operational complexity or increasing the risk of outages.

* See how YugaByte [compares with other databases](https://docs.yugabyte.com/comparisons/).
* Read more about YugaByte in our [docs](https://docs.yugabyte.com/introduction/overview/).

## Supported APIs

In terms of data model and APIs, YugaByte supports the following on top of a common core data platform: 
* [Cassandra Query Language (CQL)](https://docs.yugabyte.com/api/cassandra/) - with enhancements to support ACID transactions in the works
* [Redis](https://docs.yugabyte.com/api/redis/) - as a full database with automatic sharding, clustering, elasticity
* PostgreSQL (in progress) - with linear scalability, high availability and fault tolerance

**Note**: You can run your Apache Spark applications on YugaByte DB

YugaByte DB is driver compatible with Apache Cassandra CQL and Redis - you can run existing applications written using existing open-source client drivers.

The distributed transactions feature is supported in the core data platform. The work to expose this as strongly consistent secondary indexes, multi-table/row ACID operations and SQL support is actively in progress. You can follow the progress of these features in our [community forum](https://forum.yugabyte.com/).

## Getting Started

Here are a few resources for getting started with YugaByte:

* [Quick start guide](http://docs.yugabyte.com/quick-start/) - install, create a local cluster and read/write from YugaByte.
* [Explore core features](https://docs.yugabyte.com/explore/) - automatic sharding & rebalancing, linear scalability, fault tolerance, tunable reads etc.
* [Real world apps](https://docs.yugabyte.com/develop/realworld-apps/) - how real-world, end-to-end applications can be built using YugaByte DB.
* [Architecture docs](https://docs.yugabyte.com/architecture/) - to understand how YugaByte was designed and how it works

Cannot find what you are looking for? Have a question? We love to hear from you - please post your questions or comments to our [community forum](https://forum.yugabyte.com).

## Developing Apps

Here is a tutorial on implementing a simple Hello World application for YugaByte CQL and Redis in different languages:
* [Java](https://docs.yugabyte.com/develop/client-drivers/java/) using Maven
* [NodeJS](https://docs.yugabyte.com/develop/client-drivers/nodejs/)
* [Python](https://docs.yugabyte.com/develop/client-drivers/python/)

We are constantly adding documentation on how to build apps using the client drivers in various languages, as well as the ecosystem integrations we support. Please see [our app-development docs](https://docs.yugabyte.com/develop/) for the latest information.

Once again, please post your questions or comments to our [community forum](https://forum.yugabyte.com) if you need something.

## Building YugaByte code

### Prerequisites for CentOS 7

CentOS 7 is the main recommended development and production platform for YugaByte.

Update packages on your system, install development tools and additional packages:

```bash
sudo yum update
sudo yum groupinstall -y 'Development Tools'
sudo yum install -y ruby perl-Digest epel-release ccache python python-pip
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
dependencies on CentOS. We install Linuxbrew in a separate directory, `~/.linuxbrew-yb-build`,
so that it does not conflict with any other Linuxbrew installation on your workstation, and does
not contain any unnecessary packages that would interfere with the build.

```
git clone git@github.com:linuxbrew/brew.git ~/.linuxbrew-yb-build
~/.linuxbrew-yb-build/bin/brew install autoconf automake boost flex gcc libtool openssl libuuid maven cmake
```

We don't need to add `~/.linuxbrew-yb-build/bin` to PATH. The build scripts will automatically
discover this Linuxbrew installation.

### Prerequisites for Mac OS X

Install [Homebrew](https://brew.sh/):

```bash
/usr/bin/ruby -e "$(
  curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
```

Install the following packages using Homebrew:
```
brew install autoconf automake bash bison boost ccache cmake coreutils flex gnu-tar libtool \
             pkg-config pstree wget zlib
```

Also YugaByte build scripts rely on Bash 4. Make sure that `which bash` outputs
`/usr/local/bin/bash` before proceeding. You may need to put `/usr/local/bin` as the first directory
on PATH in your `~/.bashrc` to achieve that.

### Prerequisites for drivers and sample apps

YugaByte core is written in C++, but the repository contains Java code needed to run sample
applications. To build the Java part, you need:
* JDK 8
* [Apache Maven](https://maven.apache.org/).

Also make sure Maven's `bin` directory is added to your PATH, e.g. by adding to your `~/.bashrc`
```
export PATH=$HOME/tools/apache-maven-3.5.0/bin:$PATH
```
if you've installed Maven into `~/tools/apache-maven-3.5.0`.

For building YugaByte Java code, you'll need to install Java and Apache Maven.

**Java driver**

YugaByte and Apache Cassandra use different approaches to split data between nodes. In order to
route client requests to the right server without extra hops, we provide a [custom
load balancing policy](https://goo.gl/At7kvu) in [our modified version
](https://github.com/yugabyte/cassandra-java-driver) of Datastax's Apache Cassandra Java driver.

The latest version of our driver is available on Maven Central. You can build your application
using our driver by adding the following Maven dependency to your application:

```
<dependency>
  <groupId>com.yugabyte</groupId>
  <artifactId>cassandra-driver-core</artifactId>
  <version>3.2.0-yb-12</version>
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

## Reporting Issues

Please use [GitHub issues](https://github.com/YugaByte/yugabyte-db/issues) to report issues.
Also feel free to post on the [YugaByte Community Forum](http://forum.yugabyte.com).

## Contributing

We accept contributions as GitHub pull requests. Our code style is available
[here](https://goo.gl/Hkt5BU)
(mostly based on [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)).

## License

YugaByte Community Edition is distributed under an Apache 2.0 license. See the
[LICENSE.txt](https://github.com/YugaByte/yugabyte-db/blob/master/LICENSE.txt) file for
details.
