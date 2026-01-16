---
title: YugabyteDB Quick start for macOS
headerTitle: Quick start
linkTitle: Quick start
headcontent: Get started in less than 5 minutes in the cloud or on your desktop
description: Get started using YugabyteDB in less than five minutes on macOS.
aliases:
  - /stable/quick-start/create-local-cluster/
  - /stable/quick-start/install/
  - /stable/quick-start/
  - /stable/develop/tutorials/quick-start/macos/
layout: single
type: docs
rightNav:
  hideH4: true
unversioned: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../../quick-start-yugabytedb-managed/" class="nav-link">
      <img src="/icons/cloud.svg" alt="Cloud Icon">
      Use a cloud cluster
    </a>
  </li>
  <li class="active">
    <a href="../macos/" class="nav-link">
      <img src="/icons/database.svg" alt="Server Icon">
      Use a local cluster
    </a>
  </li>
</ul>

The local cluster setup on a single host is intended for development and learning. For production deployment, performance benchmarking, or deploying a true multi-node on multi-host setup, see [Deploy YugabyteDB](/stable/deploy/).

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li class="active">
    <a href="../macos/" class="nav-link">
      <i class="fa-brands fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="../linux/" class="nav-link">
      <i class="fa-brands fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>
  <li>
    <a href="../docker/" class="nav-link">
      <i class="fa-brands fa-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>
  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
</ul>

## Install YugabyteDB

Installing YugabyteDB involves completing [prerequisites](#prerequisites) and [downloading the packaged database](#download).

### Prerequisites

{{% readfile "include-prerequisites-macos.md" %}}

#### Set file limits

Because each tablet maps to its own file, you can create a very large number of files in the current shell by experimenting with several hundred tables and several tablets per table. Execute the following command to ensure that the limit is set to a large number:

```sh
launchctl limit
```

It is recommended to have at least the following soft and hard limits:

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

If this file does not exist, create the following two files (this will require sudo access):

- `/Library/LaunchDaemons/limit.maxfiles.plist` and insert the following:

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

- `/Library/LaunchDaemons/limit.maxproc.plist` and insert the following:

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

Ensure that the `plist` files are owned by `root:wheel` and have permissions `-rw-r--r--`. To take effect, you need to reboot your computer or run the following commands:

```sh
sudo launchctl load -w /Library/LaunchDaemons/limit.maxfiles.plist
sudo launchctl load -w /Library/LaunchDaemons/limit.maxproc.plist
```

You might need to `unload` the service before loading it.

### Download

You download YugabyteDB as follows:

{{< tabpane text=true >}}

{{% tab header="macOS x86" lang="x86" %}}

1. Download the YugabyteDB `tar.gz` file by executing the following `wget` command:

    ```sh
    wget https://software.yugabyte.com/releases/{{< yb-version version="stable">}}/yugabyte-{{< yb-version version="stable" format="build">}}-darwin-x86_64.tar.gz
    echo "$(curl -L https://software.yugabyte.com/releases/{{< yb-version version="stable">}}/yugabyte-{{< yb-version version="stable" format="build">}}-darwin-x86_64.tar.gz.sha) *yugabyte-{{< yb-version version="stable" format="build">}}-darwin-x86_64.tar.gz" | shasum --check
    ```

1. Extract the package and then change directories to the YugabyteDB home, as follows:

    ```sh
    tar xvfz yugabyte-{{< yb-version version="stable" format="build">}}-darwin-x86_64.tar.gz && cd yugabyte-{{< yb-version version="stable">}}/
    ```

{{% /tab %}}

{{% tab header="macOS ARM" lang="arm" %}}

1. Download the YugabyteDB `tar.gz` file by executing the following `wget` command:

    ```sh
    wget https://software.yugabyte.com/releases/{{< yb-version version="stable">}}/yugabyte-{{< yb-version version="stable" format="build">}}-darwin-arm64.tar.gz
    echo "$(curl -L https://software.yugabyte.com/releases/{{< yb-version version="stable">}}/yugabyte-{{< yb-version version="stable" format="build">}}-darwin-arm64.tar.gz.sha) *yugabyte-{{< yb-version version="stable" format="build">}}-darwin-arm64.tar.gz" | shasum --check
    ```

1. Extract the package and then change directories to the YugabyteDB home, as follows:

    ```sh
    tar xvfz yugabyte-{{< yb-version version="stable" format="build">}}-darwin-arm64.tar.gz && cd yugabyte-{{< yb-version version="stable">}}/
    ```

{{% /tab %}}

{{< /tabpane >}}

## Create a local cluster

Use the [yugabyted](/stable/reference/configuration/yugabyted/) utility to create and manage universes.

{{< tabpane text=true >}}

  {{% tab header="macOS Pre-Monterey" lang="Pre-Monterey" %}}

On macOS pre-Monterey, create a single-node local cluster with a replication factor (RF) of 1 by running the following command:

```sh
./bin/yugabyted start
```

  {{% /tab %}}

  {{% tab header="macOS Monterey" lang="Monterey" %}}

macOS Monterey enables AirPlay receiving by default, which listens on port 7000. This conflicts with YugabyteDB and causes `yugabyted start` to fail. Use the [--master_webserver_port flag](/stable/reference/configuration/yugabyted/#advanced-flags) when you start the cluster to change the default port number, as follows:

```sh
./bin/yugabyted start --master_webserver_port=9999
```

Alternatively, you can disable AirPlay receiving, then start YugabyteDB normally, and then, optionally, re-enable AirPlay receiving.

  {{% /tab %}}

{{< /tabpane >}}

{{< readfile "/stable/quick-start/include-connect.md" >}}

## Build an application

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). This section shows how to connect applications to your cluster using your favorite programming language.

### Choose your language

<details><summary>Choose the language you want to use to build your application.</summary><br>
{{< readfile "/stable/quick-start-yugabytedb-managed/quick-start-buildapps-include.md" >}}
</details>

## Migrate from PostgreSQL

For PostgreSQL users seeking to transition to a modern, horizontally scalable database solution with built-in resilience, YugabyteDB offers a seamless lift-and-shift approach that ensures compatibility with PostgreSQL syntax and features while providing the scalability benefits of distributed SQL.

YugabyteDB enables midsize applications running on single-node instances to effortlessly migrate to a fully distributed database environment. As applications grow, YugabyteDB seamlessly transitions to distributed mode, allowing for massive scaling capabilities.

[YugabyteDB Voyager](/stable/yugabyte-voyager/) simplifies the end-to-end database migration process, including cluster setup, schema migration, and data migration. It supports migrating data from PostgreSQL, MySQL, and Oracle databases to various YugabyteDB offerings, including Aeon, Anywhere, and the core open-source database.

You can [install](/stable/yugabyte-voyager/install-yb-voyager/) YugabyteDB Voyager on different operating systems such as RHEL, Ubuntu, macOS, or deploy it via Docker or Airgapped installations.

In addition to [offline migration](/stable/yugabyte-voyager/migrate/migrate-steps/), the latest release of YugabyteDB Voyager introduces [live, non-disruptive migration](/stable/yugabyte-voyager/migrate/live-migrate/) from PostgreSQL, along with new live migration workflows featuring [fall-forward](/stable/yugabyte-voyager/migrate/live-fall-forward/) and [fall-back](/stable/yugabyte-voyager/migrate/live-fall-back/) capabilities.

Furthermore, Voyager previews a powerful migration assessment that scans existing applications and databases. This detailed assessment provides organizations with valuable insights into the readiness of their applications, data, and schema for migration, thereby accelerating modernization efforts.

## Next step

[Explore YugabyteDB](/stable/explore/)
