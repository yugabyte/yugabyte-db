---
title: YugabyteDB Quick start for Linux
headerTitle: Quick start
linkTitle: Quick start
headcontent: Get started in less than 5 minutes in the cloud or on your desktop
description: Get started using YugabyteDB in less than five minutes on Linux.
aliases:
  - /quick-start/linux/
  - /stable/develop/tutorials/quick-start/linux/
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
  <li>
    <a href="../macos/" class="nav-link">
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

Because each tablet maps to its own file, you can create a very large number of files in the current shell by experimenting with several hundred tables and several tablets per table. You need to [configure ulimit values](/stable/deploy/manual-deployment/system-config/#set-ulimits).

### Download

The following instructions are for downloading the Preview release of YugabyteDB, which is recommended for development and testing only. For other versions, see [Releases](/stable/releases/).

YugabyteDB supports both x86 and ARM (aarch64) CPU architectures. Download packages ending in `x86_64.tar.gz` to run on x86, and packages ending in `aarch64.tar.gz` to run on ARM.

Download and extract YugabyteDB as follows:

{{< tabpane text=true >}}

  {{% tab header="x86" lang="x86" %}}

```sh
wget https://software.yugabyte.com/releases/{{< yb-version version="stable">}}/yugabyte-{{< yb-version version="stable" format="build">}}-linux-x86_64.tar.gz
echo "$(curl -L https://software.yugabyte.com/releases/{{< yb-version version="stable">}}/yugabyte-{{< yb-version version="stable" format="build">}}-linux-x86_64.tar.gz.sha) *yugabyte-{{< yb-version version="stable" format="build">}}-linux-x86_64.tar.gz" | shasum --check && \
tar xvfz yugabyte-{{< yb-version version="stable" format="build">}}-linux-x86_64.tar.gz && cd yugabyte-{{< yb-version version="stable">}}/
```

  {{% /tab %}}

  {{% tab header="aarch64" lang="aarch64" %}}

```sh
wget https://software.yugabyte.com/releases/{{< yb-version version="stable">}}/yugabyte-{{< yb-version version="stable" format="build">}}-el8-aarch64.tar.gz
echo "$(curl -L https://software.yugabyte.com/releases/{{< yb-version version="stable">}}/yugabyte-{{< yb-version version="stable" format="build">}}-el8-aarch64.tar.gz.sha) *yugabyte-{{< yb-version version="stable" format="build">}}-el8-aarch64.tar.gz" | shasum --check && \
tar xvfz yugabyte-{{< yb-version version="stable" format="build">}}-el8-aarch64.tar.gz && cd yugabyte-{{< yb-version version="stable">}}/
```

  {{% /tab %}}

{{< /tabpane >}}

### Configure

To configure YugabyteDB, run the following shell script:

```sh
./bin/post_install.sh
```

## Create a local cluster

Use the [yugabyted](/stable/reference/configuration/yugabyted/) utility to create and manage universes.

To create a single-node local cluster with a replication factor (RF) of 1, run the following command:

```sh
./bin/yugabyted start --advertise_address 127.0.0.1
```

Note: By default, yugabyted on Linux-based machines binds to the internal IP address. If it fails to do so, set the --advertise_address flag.

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
