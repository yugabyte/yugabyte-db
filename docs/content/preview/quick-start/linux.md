---
title: YugabyteDB Quick start for Linux
headerTitle: Quick start
linkTitle: Linux
description: Test YugabyteDB's APIs and core features by creating a local cluster on a single host.
headcontent: Create a local cluster on a single host
aliases:
  - /quick-start/linux/
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
    <a href="../../quick-start/" class="nav-link">
      <img src="/icons/database.svg" alt="Server Icon">
      Use a local cluster
    </a>
  </li>
</ul>

The local cluster setup on a single host is intended for development and learning. For production deployment, performance benchmarking, or deploying a true multi-node on multi-host setup, see [Deploy YugabyteDB](../../deploy/).

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../" class="nav-link">
      <i class="fa-brands fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li class="active">
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

Installing YugabyteDB involves completing [prerequisites](#prerequisites) and [downloading the YugabyteDB package](#download).

### Prerequisites

{{% readfile "include-prerequisites-linux.md" %}}

#### ulimits

Because each tablet maps to its own file, you can create a very large number of files in the current shell by experimenting with several hundred tables and several tablets per table. You need to [configure ulimit values](../../deploy/manual-deployment/system-config/#ulimits).

### Download

YugabyteDB supports both x86 and ARM (aarch64) CPU architectures. Download packages ending in `x86_64.tar.gz` to run on x86, and packages ending in `aarch64.tar.gz` to run on ARM.

The following instructions are for downloading the Preview release of YugabyteDB, which is recommended for development and testing only. For other versions, see [Releases](../../releases/).

Download YugabyteDB as follows:

1. Download the YugabyteDB package using one of the following `wget` commands:

    ```sh
    wget https://downloads.yugabyte.com/releases/{{< yb-version version="preview">}}/yugabyte-{{< yb-version version="preview" format="build">}}-linux-x86_64.tar.gz
    ```

    Or:

    ```sh
    wget https://downloads.yugabyte.com/releases/{{< yb-version version="preview">}}/yugabyte-{{< yb-version version="preview" format="build">}}-el8-aarch64.tar.gz
    ```

1. Extract the package and then change directories to the YugabyteDB home.

    ```sh
    tar xvfz yugabyte-{{< yb-version version="preview" format="build">}}-linux-x86_64.tar.gz && cd yugabyte-{{< yb-version version="preview">}}/
    ```

    Or:

    ```sh
    tar xvfz yugabyte-{{< yb-version version="preview" format="build">}}-el8-aarch64.tar.gz && cd yugabyte-{{< yb-version version="preview">}}/
    ```

### Configure

To configure YugabyteDB, run the following shell script:

```sh
./bin/post_install.sh
```

## Create a local cluster

Use the [yugabyted](../../reference/configuration/yugabyted/) utility to create and manage universes.

To create a single-node local cluster with a replication factor (RF) of 1, run the following command:

```sh
./bin/yugabyted start
```

{{% includeMarkdown "./include-connect.md" %}}

## Build an application

Applications connect to and interact with YugabyteDB using API client libraries (also known as client drivers). This section shows how to connect applications to your cluster using your favorite programming language.

### Choose your language

{{< readfile "/preview/quick-start-yugabytedb-managed/quick-start-buildapps-include.md" >}}

## Next step

[Explore YugabyteDB](../../explore/)
