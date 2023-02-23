---
title: Build from source code on Ubuntu
headerTitle: Build the source code
linkTitle: Build the source
description: Build YugabyteDB from source code on Ubuntu.
image: /images/section_icons/index/quick_start.png
headcontent: Build the source code.
menu:
  preview:
    identifier: build-from-src-4-ubuntu
    parent: core-database
    weight: 2912
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="{{< relref "./build-from-src-almalinux.md" >}}" class="nav-link">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
      AlmaLinux
    </a>
  </li>

  <li >
    <a href="{{< relref "./build-from-src-macos.md" >}}" class="nav-link">
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
    <a href="{{< relref "./build-from-src-ubuntu.md" >}}" class="nav-link active">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
      Ubuntu
    </a>
  </li>

</ul>

{{< note title="Note" >}}

AlmaLinux 8 is the recommended Linux development platform for YugabyteDB.

{{< /note >}}

The following instructions are for Ubuntu 20.04 or Ubuntu 22.04.

## Install necessary packages

Update packages on your system, install development tools and additional packages:

```sh
sudo apt update
sudo apt upgrade -y
packages=(
  autoconf
  build-essential
  curl
  gettext
  git
  locales
  ninja-build
  pkg-config
  rsync
)
sudo apt install -y "${packages[@]}"
sudo locale-gen en_US.UTF-8
```

### Python 3

{{% readfile "includes/python.md" %}}

```sh
sudo apt install -y libffi-dev python3 python3-dev python3-venv
```

### CMake 3

{{% readfile "includes/cmake.md" %}}

The CMake version in the package manager is too old (3.16), so manually download a release as follows:

```sh
mkdir ~/tools
curl -L "https://github.com/Kitware/CMake/releases/download/v3.25.2/cmake-3.25.2-linux-x86_64.tar.gz" | tar xzC ~/tools
# Also add the following line to your .bashrc or equivalent.
export PATH="$HOME/tools/cmake-3.25.2-linux-x86_64/bin:$PATH"
```

### /opt/yb-build

{{% readfile "includes/opt-yb-build.md" %}}

### Ninja (optional)

Use [Ninja][ninja] for faster builds.

```sh
sudo apt install -y ninja-build
```

[ninja]: https://ninja-build.org

### Ccache (optional)

Use [Ccache][ccache] for faster builds.

```sh
sudo apt install -y ccache
# Also add the following line to your .bashrc or equivalent.
export YB_CCACHE_DIR="$HOME/.cache/yb_ccache"
```

[ccache]: https://ccache.dev

### Java

{{% readfile "includes/java.md" %}}

The `maven` package satisfies both requirements.

```sh
sudo apt install -y maven
```

### yugabyted-ui

{{% readfile "includes/yugabyted-ui.md" %}}

```sh
sudo apt install -y npm golang-1.18
# Also add the following line to your .bashrc or equivalent.
export PATH="/usr/lib/go-1.18/bin:$PATH"
```

## Build the code

{{% readfile "includes/build-the-code.md" %}}

### Build release package (optional)

[Satisfy requirements for building yugabyted-ui](#yugabyted-ui).

Install the following additional packages:

```sh
sudo apt install -y file patchelf
```

Run the `yb_release` script to build a release package:

```output.sh
$ ./yb_release
......
2023-02-17 01:26:37,156 [yb_release.py:299 INFO] Generated a package at '/home/user/code/yugabyte-db/build/yugabyte-2.17.2.0-ede2a2619ea8470064a5a2c0d7fa510dbee3ce81-release-clang15-ubuntu20-x86_64.tar.gz'
```

{{% readfile "includes/ulimit.md" %}}
