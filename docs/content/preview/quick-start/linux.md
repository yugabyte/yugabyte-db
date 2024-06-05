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

## Migrate from PostgreSQL

For PostgreSQL users seeking to transition to a modern, horizontally scalable database solution with built-in resilience, YugabyteDB offers a seamless lift-and-shift approach that ensures compatibility with PostgreSQL syntax and features while providing the scalability benefits of distributed SQL.

YugabyteDB enables midsize applications running on single-node instances to effortlessly migrate to a fully distributed database environment. As applications grow, YugabyteDB seamlessly transitions to distributed mode, allowing for massive scaling capabilities.

[YugabyteDB Voyager](../../yugabyte-voyager/) simplifies the end-to-end database migration process, including cluster setup, schema migration, and data migration. It supports migrating data from PostgreSQL, MySQL, and Oracle databases to various YugabyteDB offerings, including Managed, Anywhere, and the core open-source database.

You can [install](../../yugabyte-voyager/install-yb-voyager/) YugabyteDB Voyager on different operating systems such as RHEL, Ubuntu, macOS, or deploy it via Docker or Airgapped installations.

In addition to [offline migration](../../yugabyte-voyager/migrate/migrate-steps/), the latest release of YugabyteDB Voyager introduces [live, non-disruptive migration](../../yugabyte-voyager/migrate/live-migrate/) from PostgreSQL, along with new live migration workflows featuring [fall-forward](../../yugabyte-voyager/migrate/live-fall-forward/) and [fall-back](../../yugabyte-voyager/migrate/live-fall-back/) capabilities.

Furthermore, Voyager previews a powerful migration assessment that scans existing applications and databases. This detailed assessment provides organizations with valuable insights into the readiness of their applications, data, and schema for migration, thereby accelerating modernization efforts.

## Next step

[Explore YugabyteDB](../../explore/)
