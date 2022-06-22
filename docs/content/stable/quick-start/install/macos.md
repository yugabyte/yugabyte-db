---
title: Install YugabyteDB on macOS
headerTitle: 1. Install YugabyteDB
linkTitle: 1. Install YugabyteDB
description: Download and install YugabyteDB on macOS in less than five minutes.
menu:
  stable:
    parent: quick-start
    name: 1. Install YugabyteDB
    identifier: install-1-macos
    weight: 110
type: page
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../macos/" class="nav-link active">
      <i class="fab fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>

  <li >
    <a href="../linux/" class="nav-link">
      <i class="fab fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>

  <li >
    <a href="../docker/" class="nav-link">
      <i class="fab fa-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>

  <li >
    <a href="../kubernetes/" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

## Prerequisites

1. <i class="fab fa-apple" aria-hidden="true"></i> macOS 10.12 or later.

1. Verify that you have Python 2 or 3 installed.

    ```sh
    $ python --version
    ```

    ```output
    Python 3.7.3
    ```

1. `wget` or `curl` is available.

    The instructions use the `wget` command to download files. If you prefer to use `curl` (included in macOS), you can replace `wget` with `curl -O`.

    To install `wget` on your Mac, you can run the following command if you use Homebrew:

    ```sh
    $ brew install wget
    ```

1. Each tablet maps to its own file, so if you experiment with a few hundred tables and a few tablets per table, you can soon end up creating a large number of files in the current shell. Make sure that this command shows a big enough value.

    ```sh
    $ launchctl limit
    ```

    We recommend having at least the following soft and hard limits.

    ```output
    maxproc     2500        2500
    maxfiles    1048576     1048576
    ```

    Edit `/etc/sysctl.conf`, if it exists, to include the following:

    ```sh
    kern.maxfiles=1048576
    kern.maxproc=2500
    kern.maxprocperuid=2500
    kern.maxfilesperproc=1048576
    ```

    If this file does not exist, create the following two files:
    \
    Create `/Library/LaunchDaemons/limit.maxfiles.plist` and insert the following:

    ```xml
    <?xml version="1.0" encoding="UTF-8"?>
    <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
    <plist version="1.0">
      <dict>
        <key>Label</key>
          <string>limit.maxfiles</string>
        <key>ProgramArguments</key>
          <array>
            <string>launchctl</string>
            <string>limit</string>
            <string>maxfiles</string>
            <string>1048576</string>
            <string>1048576</string>
          </array>
        <key>RunAtLoad</key>
          <true/>
        <key>ServiceIPC</key>
          <false/>
      </dict>
    </plist>
    ```

    \
    Create `/Library/LaunchDaemons/limit.maxproc.plist` and insert the following:

    ```xml
    <?xml version="1.0" encoding="UTF-8"?>
    <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
    <plist version="1.0">
      <dict>
        <key>Label</key>
          <string>limit.maxproc</string>
        <key>ProgramArguments</key>
          <array>
            <string>launchctl</string>
            <string>limit</string>
            <string>maxproc</string>
            <string>2500</string>
            <string>2500</string>
          </array>
        <key>RunAtLoad</key>
          <true/>
        <key>ServiceIPC</key>
          <false/>
      </dict>
    </plist>
    ```

    \
    Ensure that the `plist` files are owned by `root:wheel` and have permissions `-rw-r--r--`. To take effect, you need to reboot your computer or run the following commands:

    ```sh
    $ sudo launchctl load -w /Library/LaunchDaemons/limit.maxfiles.plist
    $ sudo launchctl load -w /Library/LaunchDaemons/limit.maxproc.plist
    ```

    \
    You might need to `unload` the service before loading it.

## Download YugabyteDB

1. Download the YugabyteDB `tar.gz` file using the following `wget` command.

    ```sh
    $ wget https://downloads.yugabyte.com/releases/{{< yb-version version="stable">}}/yugabyte-{{< yb-version version="stable" format="build">}}-darwin-x86_64.tar.gz
    ```

1. Extract the package and then change directories to the YugabyteDB home.

    ```sh
    $ tar xvfz yugabyte-{{< yb-version version="stable" format="build">}}-darwin-x86_64.tar.gz && cd yugabyte-{{< yb-version version="stable">}}/
    ```

## Next step

[Create a local cluster](../../create-local-cluster/macos/)
