---
title: Build from source code on CentOS
headerTitle: Build the source code
linkTitle: Build the source
description: Build YugabyteDB from source code on CentOS.
image: /images/section_icons/index/quick_start.png
headcontent: Build the source code.
menu:
  preview:
    identifier: build-from-src-2-centos
    parent: core-database
    weight: 2912
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="{{< relref "./build-from-src-macos.md" >}}" class="nav-link">
      <i class="fa-brands fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>

  <li >
    <a href="{{< relref "./build-from-src-centos.md" >}}" class="nav-link active">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
      CentOS
    </a>
  </li>

  <li >
    <a href="{{< relref "./build-from-src-ubuntu.md" >}}" class="nav-link">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
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
sudo yum update
sudo yum groupinstall -y 'Development Tools'
sudo yum install -y ruby perl-Digest epel-release ccache git python2-pip python-devel python3 python3-pip python3-devel which
sudo yum install -y cmake3 ctest3 ninja-build
```

### Build tools

Make sure `cmake`/`ctest` binaries are at least version 3. On CentOS, one way to achieve this is to symlink them into `/usr/local/bin`.

```sh
sudo ln -s /usr/bin/cmake3 /usr/local/bin/cmake
sudo ln -s /usr/bin/ctest3 /usr/local/bin/ctest
```

You could also symlink them into another directory that is on your `PATH`.

{{< note title="Note" >}}

We also use [Linuxbrew](https://github.com/linuxbrew/brew) to provide some of the third-party dependencies on CentOS.
Linuxbrew allows us to create a portable package that contains its own copy of glibc and can be installed on most Linux distributions.
However, we are transitioning away from using Linuxbrew and towards native toolchains on various platforms.

Our build scripts may automatically install Linuxbrew in a directory such as `/opt/yb-build/brew/linuxbrew-<version>`.
There is no need to add any of those directories to `PATH`.

{{< /note >}}

### Java

{{% readfile "includes/java.md" %}}

## Build the code

{{% readfile "includes/build-the-code.md" %}}

### Build release package

Run the `yb_release` script to build a release package:

```output.sh
$ ./yb_release
......
2020-10-27 20:52:27,978 [yb_release.py:283 INFO] Generated a package at '/home/user/code/yugabyte-db/build/yugabyte-2.5.1.0-8696bc05a97c4907b53d6446b5bfa7acb28ceef5-release-centos-x86_64.tar.gz'
```
