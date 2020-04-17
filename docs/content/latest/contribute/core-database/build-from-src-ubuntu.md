---
title: Build from source code on Ubuntu
headerTitle: Build the source code
linkTitle: Build the source
description: Build YugabyteDB from source code on Ubuntu.
image: /images/section_icons/index/quick_start.png
headcontent: Build the source code.
type: page
menu:
  latest:
    identifier: build-from-src-3-ubuntu
    parent: core-database
    weight: 2912
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/contribute/core-database/build-from-src-macos" class="nav-link">
      <i class="fab fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>

  <li >
    <a href="/latest/contribute/core-database/build-from-src-centos" class="nav-link">
      <i class="fab fa-linux" aria-hidden="true"></i>
      CentOS
    </a>
  </li>

  <li >
    <a href="/latest/contribute/core-database/build-from-src-ubuntu" class="nav-link active">
      <i class="fab fa-linux" aria-hidden="true"></i>
      Ubuntu
    </a>
  </li>

</ul>

{{< note title="Note" >}}

CentOS 7 is the recommended Linux development and production platform for YugabyteDB.

{{< /note >}}

## Installing necessary packages

Update packages on your system, install development tools and additional packages:

```sh
sudo apt-get update
sudo apt-get install uuid-dev libbz2-dev libreadline-dev maven ninja-build
```

## Getting build tools ready

Make sure `cmake` / `ctest` binaries are at least version 3. On Ubuntu, one way to achieve this is to symlink them into `/usr/local/bin`.

```sh
sudo ln -s /usr/bin/cmake3 /usr/local/bin/cmake
sudo ln -s /usr/bin/ctest3 /usr/local/bin/ctest
```

You could also symlink them into another directory that is on your `PATH`.

{{< note title="Note" >}}

We also use [Linuxbrew](https://github.com/linuxbrew/brew) to provide some of the third-party dependencies on Ubuntu.

During the build we install Linuxbrew in a separate directory, `~/.linuxbrew-yb-build/linuxbrew-<version>`, so that it does not conflict with any other Linuxbrew installation on your workstation, and does not contain any unnecessary packages that would interfere with the build.

We don't need to add `~/.linuxbrew-yb-build/linuxbrew-<version>/bin` to `PATH`. The build scripts will automatically discover this Linuxbrew installation.

{{< /note >}}


## Building the code

Assuming this repository is checked out in `~/code/yugabyte-db`, do the following:

```sh
cd ~/code/yugabyte-db
./yb_build.sh release
```

The above command will build the release configuration, put the C++ binaries in `build/release-gcc-dynamic-community`, and will also create the `build/latest` symlink to that directory.

{{< tip title="Tip" >}}

You can find the binaries you just built in `build/latest` directory.

{{< /tip >}}

For Linux, it will first make sure our custom Linuxbrew distribution is installed into `~/.linuxbrew-yb-build/linuxbrew-<version>`.

## Build Java code

YugabyteDB core is written in C++, but the repository contains Java code needed to run sample applications. To build the Java part, you need:

* JDK 8
* [Apache Maven](https://maven.apache.org/).

Also make sure Maven's bin directory is added to your `PATH` (for example, by adding to your `~/.bashrc`). See the example below (if you've installed Maven into `~/tools/apache-maven-3.5.0`)

```sh
export PATH=$HOME/tools/apache-maven-3.5.0/bin:$PATH
```

For building YugabyteDB Java code, you'll need to install Java and Apache Maven.
