---
title: Build from source code on macOS
headerTitle: Build the source code
linkTitle: Build the source
description: Build YugabyteDB from source code on macOS.
image: /images/section_icons/index/quick_start.png
headcontent: Build the source code on macOS, CentOS, and Ubuntu.
aliases:
  - /preview/contribute/core-database/build-from-src
menu:
  preview:
    identifier: build-from-src-1-macos
    parent: core-database
    weight: 2912
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="{{< relref "./build-from-src-macos.md" >}}" class="nav-link active">
      <i class="fa-brands fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>

  <li >
    <a href="{{< relref "./build-from-src-centos.md" >}}" class="nav-link">
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

CentOS 7 is the recommended Linux distribution for development and production platform for YugabyteDB.

{{< /note >}}

## Install necessary packages

First, install [Homebrew](https://brew.sh/), if you do not already have it. Homebrew is used to install the other required packages.

```sh
/usr/bin/ruby -e "$(
  curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
```

Install the following packages using Homebrew:

```sh
brew install autoconf automake bash ccache cmake coreutils gnu-tar libtool \
             maven ninja pkg-config pstree wget python
```

{{< note title="Note" >}}

YugabyteDB build scripts require at least Bash version 4. Make sure that `bash --version` outputs a version of 4 or higher before proceeding. You may need to put `/usr/local/bin` (Intel) or `/opt/homebrew/bin` (Apple Silicon) as the first directory on `PATH` in your `~/.bashrc` to achieve that.

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
2020-10-27 13:55:40,856 [yb_release.py:283 INFO] Generated a package at '/Users/me/code/yugabyte-db/build/yugabyte-2.5.1.0-6ab8013159fdca00ced7e6f5d2f98cacac6a536a-release-darwin-x86_64.tar.gz'
```
