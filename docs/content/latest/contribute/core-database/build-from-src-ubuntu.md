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

## Install necessary packages

Update packages on your system, install development tools and additional packages:

```sh
sudo apt-get update
sudo apt-get install uuid-dev libbz2-dev libreadline-dev maven ninja-build \
                     cmake curl rsync python-pip zip autoconf libtool \
                     pkg-config libssl1.0-dev libicu-dev bison flex
```

Assuming this repository is checked out in `~/code/yugabyte-db`, do the following:

```sh
cd ~/code/yugabyte-db
./yb_build.sh release
```

The above command will build the release configuration, put the C++ binaries in `build/release-gcc-dynamic-ninja`, and will also create the `build/latest` symlink to that directory.


{{< note title="Note" >}}
If you are getting errors in the form of:
```
uild/release-gcc-dynamic-ninja/postgres_build/src/backend/libpq/be-secure-openssl.o: In function `my_sock_read':
src/postgres/src/backend/libpq/be-secure-openssl.c:665: undefined reference to `BIO_get_data'
build/release-gcc-dynamic-ninja/postgres_build/src/backend/libpq/be-secure-openssl.o: In function `my_sock_write':
src/postgres/src/backend/libpq/be-secure-openssl.c:685: undefined reference to `BIO_get_data'
```
The code is probably not finding the right path for libssl1.0. Try a clean build `./yb_build.sh --clean release`.  
If that doesn't work, look into your $PATH if some other openssl version path is being used.  
{{< /note >}}



{{< tip title="Tip" >}}

You can find the binaries you just built in `build/latest` directory.

{{< /tip >}}

## Build Java code

YugabyteDB core is written in C++, but the repository contains Java code needed to run sample applications. To build the Java part, you need:

* JDK 8
* [Apache Maven](https://maven.apache.org/).

Also make sure Maven's bin directory is added to your `PATH` (for example, by adding to your `~/.bashrc`). See the example below (if you've installed Maven into `~/tools/apache-maven-3.5.0`)

```sh
export PATH=$HOME/tools/apache-maven-3.5.0/bin:$PATH
```

For building YugabyteDB Java code, you'll need to install Java and Apache Maven.
