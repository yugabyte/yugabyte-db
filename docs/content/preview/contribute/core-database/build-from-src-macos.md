---
title: Build from source code on macOS
headerTitle: Build the source code
linkTitle: Build the source
description: Build YugabyteDB from source code on macOS.
headcontent: Build the source code.
menu:
  preview:
    identifier: build-from-src-2-macos
    parent: core-database
    weight: 2912
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../build-from-src-almalinux/" class="nav-link">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
      AlmaLinux
    </a>
  </li>

  <li >
    <a href="../build-from-src-macos/" class="nav-link active">
      <i class="fa-brands fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>

  <li >
    <a href="../build-from-src-centos/" class="nav-link">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
      CentOS
    </a>
  </li>

  <li >
    <a href="../build-from-src-ubuntu/" class="nav-link">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
      Ubuntu
    </a>
  </li>

</ul>

{{< note title="Note" >}}

AlmaLinux 8 is the recommended Linux development platform for YugabyteDB.

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
             ninja pkg-config pstree wget python
```

{{< note title="Note" >}}

YugabyteDB build scripts require at least Bash version 4. Make sure that `bash --version` outputs a version of 4 or higher before proceeding. You may need to put `/usr/local/bin` (Intel) or `/opt/homebrew/bin` (Apple Silicon) as the first directory on `PATH` in your `~/.bashrc` to achieve that.

{{< /note >}}

### /opt/yb-build

{{% readfile "includes/opt-yb-build.md" %}}

### Java

{{% readfile "includes/java.md" %}}

Install the following packages to satisfy those requirements:

```sh
brew install openjdk@11 maven
```

Don't forget to add JDK binaries to `PATH`, ensuring this version takes precedence.
For example,

```sh
# On apple silicon mac.
echo 'export PATH="/opt/local/homebrew/opt/openjdk@11/bin:$PATH"' >>~/.bashrc
# On intel mac.
echo 'export PATH="/usr/local/opt/openjdk@11/bin:$PATH"' >>~/.bashrc
```

### yugabyted-ui

{{% readfile "includes/yugabyted-ui.md" %}}

## Build the code

{{% readfile "includes/build-the-code.md" %}}

### Build release package (optional)

Perform the following steps to build a release package:

1. [Satisfy requirements for building yugabyted-ui](#yugabyted-ui).
1. Run the `yb_release` script using the following command:

   ```sh
   ./yb_release
   ```

   ```output.sh
   ......
   2020-10-27 13:55:40,856 [yb_release.py:283 INFO] Generated a package at '/Users/me/code/yugabyte-db/build/yugabyte-2.5.1.0-6ab8013159fdca00ced7e6f5d2f98cacac6a536a-release-darwin-x86_64.tar.gz'
   ```
