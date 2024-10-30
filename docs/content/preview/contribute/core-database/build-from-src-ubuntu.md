---
title: Build from source code on Ubuntu
headerTitle: Build the source code
linkTitle: Build the source
description: Build YugabyteDB from source code on Ubuntu.
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
    <a href="../build-from-src-almalinux/" class="nav-link">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
      AlmaLinux
    </a>
  </li>

  <li >
    <a href="../build-from-src-macos/" class="nav-link">
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
    <a href="../build-from-src-ubuntu/" class="nav-link active">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
      Ubuntu
    </a>
  </li>

</ul>

{{< note title="Note" >}}

AlmaLinux 8 is the recommended Linux development platform for YugabyteDB.

{{< /note >}}

The following instructions are for Ubuntu 20.04 and 22.04.

## TLDR

{{% readfile "includes/tldr.md" %}}

```sh
# Modify to your preference:
shellrc=~/.bashrc

source <(cat /etc/os-release | grep '^VERSION_ID=')
case "$VERSION_ID" in
  20.04)
    gcc_version=10
    ;;
  22.04)
    gcc_version=11
    ;;
  *)
    echo "Unknown version $VERSION_ID"
    exit 1
esac

sudo apt update
DEBIAN_FRONTEND=noninteractive sudo apt upgrade -y
packages=(
  autoconf
  build-essential
  ccache
  curl
  file
  g++-"$gcc_version"
  gcc-"$gcc_version"
  gettext
  git
  golang-1.20
  libffi-dev
  locales
  maven
  ninja-build
  npm
  patchelf
  pkg-config
  python3
  python3-dev
  python3-venv
  rsync
)
# Avoid tzdata package configuration prompt.
if [ ! -e /etc/localtime ]; then
  sudo ln -s /usr/share/zoneinfo/Etc/UTC /etc/localtime
fi
DEBIAN_FRONTEND=noninteractive sudo apt install -y "${packages[@]}"
sudo locale-gen en_US.UTF-8
sudo mkdir /opt/yb-build

# If you'd like to use an unprivileged user for development, manually
# run/modify instructions from here onwards (change $USER, make sure shell
# variables are set appropriately when switching users).
sudo chown "$USER" /opt/yb-build
mkdir ~/tools
curl -L "https://github.com/Kitware/CMake/releases/download/v3.25.2/cmake-3.25.2-linux-x86_64.tar.gz" | tar xzC ~/tools
source <(echo 'export PATH="$HOME/tools/cmake-3.25.2-linux-x86_64/bin:$PATH"' \
         | tee -a "$shellrc")
source <(echo 'export PATH="/usr/lib/go-1.20/bin:$PATH"' \
         | tee -a "$shellrc")
source <(echo 'export YB_CCACHE_DIR="$HOME/.cache/yb_ccache"' \
         | tee -a "$shellrc")

git clone https://github.com/yugabyte/yugabyte-db
cd yugabyte-db
case "$VERSION_ID" in
  20.04)
    ./yb_release --build_args=--clang16
    ;;
  22.04)
    ./yb_release --build_args=--clang17
    ;;
esac
```

## Detailed instructions

Update and install basic development packages as follows:

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
  pkg-config
  rsync
)
sudo apt install -y "${packages[@]}"
sudo locale-gen en_US.UTF-8
```

### /opt/yb-build

{{% readfile "includes/opt-yb-build.md" %}}

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

### Java

{{% readfile "includes/java.md" %}}

Install the following package to satisfy the preceding requirements:

```sh
sudo apt install -y maven
```

### yugabyted-ui

{{% readfile "includes/yugabyted-ui.md" %}}

```sh
sudo apt install -y npm golang-1.20
# Also add the following line to your .bashrc or equivalent.
export PATH="/usr/lib/go-1.20/bin:$PATH"
```

### Ninja (optional)

{{% readfile "includes/ninja.md" %}}

```sh
sudo apt install -y ninja-build
```

### Ccache (optional)

{{% readfile "includes/ccache.md" %}}

```sh
sudo apt install -y ccache
# Also add the following line to your .bashrc or equivalent.
export YB_CCACHE_DIR="$HOME/.cache/yb_ccache"
```

### GCC (optional)

To compile with GCC, install the following packages, and adjust the version numbers to match the GCC version you plan to use.

```sh
sudo apt install -y gcc-13 g++-13
```

## Build the code

{{% readfile "includes/build-the-code.md" %}}

### Build release package (optional)

Perform the following steps to build a release package:

1. [Satisfy requirements for building yugabyted-ui](#yugabyted-ui).
1. Install additional packages using the following command:

   ```sh
   sudo apt install -y file patchelf
   ```

1. Run the `yb_release` script using the following command:

   ```sh
   ./yb_release
   ```

   ```output.sh
   ......
   2023-02-17 01:26:37,156 [yb_release.py:299 INFO] Generated a package at '/home/user/code/yugabyte-db/build/yugabyte-2.17.2.0-ede2a2619ea8470064a5a2c0d7fa510dbee3ce81-release-clang15-ubuntu20-x86_64.tar.gz'
   ```

{{% readfile "includes/ulimit.md" %}}
