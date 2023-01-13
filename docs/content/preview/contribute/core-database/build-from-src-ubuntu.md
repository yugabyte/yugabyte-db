---
title: Build from source code on Ubuntu
headerTitle: Build the source code
linkTitle: Build the source
description: Build YugabyteDB from source code on Ubuntu.
image: /images/section_icons/index/quick_start.png
headcontent: Build the source code.
menu:
  preview:
    identifier: build-from-src-3-ubuntu
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

CentOS 7 is the recommended Linux development and production platform for YugabyteDB.

{{< /note >}}

## Install necessary packages

Update packages on your system, install development tools and additional packages:

```sh
sudo apt-get update
packages=(
  autoconf
  cmake
  curl
  git
  git
  git
  libtool
  libtool
  locales
  maven
  ninja-build
  patchelf
  pkg-config
  python3-pip
  python3-venv
  rsync
  zip
)
sudo apt-get install -y "${packages[@]}"
sudo locale-gen en_US.UTF-8
```

### Java

{{% readfile "includes/java.md" %}}

## Build the code

{{% readfile "includes/build-the-code.md" %}}

### Build release package

Currently, you can only build release packages in [CentOS](../build-from-src-centos) and [macOS](../build-from-src-macos).
