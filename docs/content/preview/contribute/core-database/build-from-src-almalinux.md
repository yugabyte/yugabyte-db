---
title: Build from source code on AlmaLinux
headerTitle: Build the source code
linkTitle: Build the source
description: Build YugabyteDB from source code on AlmaLinux.
headcontent: Build the source code.
menu:
  preview:
    identifier: build-from-src-1-almalinux
    parent: core-database
    weight: 2912
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../build-from-src-almalinux/" class="nav-link active">
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
    <a href="../build-from-src-ubuntu/" class="nav-link">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
      Ubuntu
    </a>
  </li>

</ul>

{{< note title="Note" >}}

AlmaLinux 8 is the recommended Linux development platform for YugabyteDB.

{{< /note >}}

## TLDR

{{% readfile "includes/tldr.md" %}}

```sh
# Modify to your preference:
shellrc=~/.bashrc

sudo dnf update -y
sudo dnf groupinstall -y 'Development Tools'
sudo dnf -y install epel-release
packages=(
  ccache
  cmake3
  gcc-toolset-11
  gcc-toolset-11-libatomic-devel
  golang
  java-1.8.0-openjdk
  libatomic
  maven
  npm
  patchelf
  python39
  rsync
)
sudo dnf -y install "${packages[@]}"
sudo alternatives --set python3 /usr/bin/python3.9
latest_zip_url=$(curl -Ls "https://api.github.com/repos/ninja-build/ninja/releases/latest" \
                 | grep browser_download_url | grep ninja-linux.zip | cut -d \" -f 4)
curl -Ls "$latest_zip_url" | zcat | sudo tee /usr/local/bin/ninja >/dev/null
sudo chmod +x /usr/local/bin/ninja
sudo mkdir /opt/yb-build

# If you'd like to use an unprivileged user for development, manually
# run/modify instructions from here onwards (change $USER, make sure shell
# variables are set appropriately when switching users).
sudo chown "$USER" /opt/yb-build
source <(echo 'export YB_CCACHE_DIR="$HOME/.cache/yb_ccache"' \
         | tee -a "$shellrc")

git clone https://github.com/yugabyte/yugabyte-db
cd yugabyte-db
./yb_release
```

## Detailed instructions

Update and install basic development packages as follows:

```sh
sudo dnf update -y
sudo dnf groupinstall -y 'Development Tools'
sudo dnf -y install epel-release libatomic rsync
```

### /opt/yb-build

{{% readfile "includes/opt-yb-build.md" %}}

### Python 3

{{% readfile "includes/python.md" %}}

The following example installs Python 3.9.

```sh
sudo dnf install -y python39
```

In case there is more than one Python 3 version installed, ensure that `python3` refers to the right one.

```sh
sudo alternatives --set python3 /usr/bin/python3.9
sudo alternatives --display python3
python3 -V
```

### CMake 3

{{% readfile "includes/cmake.md" %}}

```sh
sudo dnf install -y cmake3
```

### Java

{{% readfile "includes/java.md" %}}

Install the following packages to satisfy the preceding requirements:

```sh
sudo dnf install -y java-1.8.0-openjdk maven
```

### yugabyted-ui

{{% readfile "includes/yugabyted-ui.md" %}}

```sh
sudo dnf install -y npm golang
```

### Ninja (optional)

{{% readfile "includes/ninja.md" %}}

The latest release can be downloaded:

```sh
latest_zip_url=$(curl -Ls "https://api.github.com/repos/ninja-build/ninja/releases/latest" \
                 | grep browser_download_url | grep ninja-linux.zip | cut -d \" -f 4)
curl -Ls "$latest_zip_url" | zcat | sudo tee /usr/local/bin/ninja >/dev/null
sudo chmod +x /usr/local/bin/ninja
```

### Ccache (optional)

{{% readfile "includes/ccache.md" %}}

```sh
sudo dnf install -y ccache
# Also add the following line to your .bashrc or equivalent.
export YB_CCACHE_DIR="$HOME/.cache/yb_ccache"
```

### GCC (optional)

To compile with GCC, install the following packages, and adjust the version numbers to match the GCC version you plan to use.

```sh
sudo dnf install -y gcc-toolset-11 gcc-toolset-11-libatomic-devel
```

## Build the code

{{% readfile "includes/build-the-code.md" %}}

### Build release package (optional)

Perform the following steps to build a release package:

1. [Satisfy requirements for building yugabyted-ui](#yugabyted-ui).
1. Install patchelf:

   ```sh
   sudo dnf install -y patchelf
   ```

1. Run the `yb_release` script using the following command:

   ```sh
   ./yb_release
   ```

   ```output.sh
   ......
   2023-02-14 04:14:16,092 [yb_release.py:299 INFO] Generated a package at '/home/user/code/yugabyte-db/build/yugabyte-2.17.2.0-b8e42eecde0e45a743d51e244dbd9662a6130cd6-release-clang15-centos-x86_64.tar.gz'
   ```

{{% readfile "includes/ulimit.md" %}}
