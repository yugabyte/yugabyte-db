# YugaByte Database

YugaByte is a cloud-native database for mission-critical applications. This repository contains the
Community Edition of the YugaByte Database. YugaByte supports Apache Cassandra Query Language and
Redis APIs, with SQL support on the roadmap.

Here are a few resources for getting started with YugaByte:
* [Community Edition Quick Start](http://docs.yugabyte.com/community-edition/quick-start/) to
  get started with YugaByte using a pre-built YugaByte Community Edition package.
* See [YugaByte Documentation](http://docs.yugabyte.com/) for architecture,
  production deployment options and languages supported. In particular, see
  [Architecture / Concepts](http://docs.yugabyte.com/architecture/concepts/) and
  [Architecture / Core Functions](http://docs.yugabyte.com/architecture/core-functions/) sections.
* See [www.yugabyte.com](https://www.yugabyte.com/) for general information about YugaByte.
* Check out the [YugaByte Community Forum](http://forum.yugabyte.com) and post your questions
  or comments.

## Build Prerequisites

### CentOS 7

CentOS 7 is the main recommended development and production platform for YugaByte.

Update packages on your system, install development tools and additional packages:

```bash
sudo yum update
sudo yum groupinstall -y 'Development Tools'
sudo yum install -y ruby perl-Digest epel-release cyrus-sasl-devel cyrus-sasl-plain ccache
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
~/.linuxbrew-yb-build/bin/brew install autoconf automake boost flex gcc libtool
```

We don't need to add `~/.linuxbrew-yb-build/bin` to PATH. The build scripts will automatically
discover this Linuxbrew installation.

### Mac OS X

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

### All platforms: Java prerequisites

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

### Cassandra Java Driver

Build and install [our modified version
](https://github.com/YugaByte/datastax-cassandra-java-driver) of Datastax's Apache Cassandra Java
driver. Clients using the unmodified driver will still work, but will be less efficient, because
YugaByte and Apache Cassandra use different approaches to splitting data between nodes. In order to
route client requests to the right server without extra hops, we provide a [custom
LoadBalancingPolicy](https://goo.gl/At7kvu) in our version of the driver.

```
mkdir -p ~/code
cd ~/code
git clone https://github.com/YugaByte/datastax-cassandra-java-driver
cd datastax-cassandra-java-driver
git checkout 3.2.0-yb-5
mvn -DskipTests -Dmaven.javadoc.skip install
```

## Building YugaByte code

Assuming this repository is checked out in `~/code/yugabyte-db`, do the following:

```
cd ~/code/yugabyte-db
./yb_build.sh release --with-assembly
```

The above command will build the release configuration, put the C++ binaries in
`build/release-gcc-dynamic-community`, and will also create the `build/latest` symlink to that
directory. Then it will build the Java code as well. The `--with-assembly` flag tells the build
script to build the `yb-sample-apps.jar` file containing sample Java apps.

## Running a Local Cluster

Now you can follow the [Communty Edition Quick Start / Create local cluster
](http://docs.yugabyte.com/community-edition/quick-start/#create-local-cluster) tutorial
to create a local cluster and test it using Apache CQL shell and Redis clients, as well as run
the provied Java sample apps.

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
