---
title: Manual software installation
headerTitle: Install software
linkTitle: 2. Install software
description: How to manually install YugabyteDB database on each node.
menu:
  v2.20:
    identifier: deploy-manual-deployment-install-software
    parent: deploy-manual-deployment
    weight: 612
type: docs
---

Installing YugabyteDB involves completing prerequisites and downloading the YugabyteDB package.

## Prerequisites

{{% readfile "/preview/quick-start/include-prerequisites-linux.md" %}}

## Download YugabyteDB

YugabyteDB supports both x86 and ARM (aarch64) CPU architectures. Download packages ending in `x86_64.tar.gz` to run on x86, and packages ending in `aarch64.tar.gz` to run on ARM.

The following instructions are for downloading the STS (standard-term support) release of YugabyteDB, which is recommended for production deployments. For other versions, see [Releases](/preview/releases/).

Download YugabyteDB as follows:

1. Download the YugabyteDB package using one of the following `wget` commands:

    ```sh
    wget https://downloads.yugabyte.com/releases/{{< yb-version version="v2.20">}}/yugabyte-{{< yb-version version="v2.20" format="build">}}-linux-x86_64.tar.gz
    ```

    Or:

    ```sh
    wget https://downloads.yugabyte.com/releases/{{< yb-version version="v2.20">}}/yugabyte-{{< yb-version version="v2.20" format="build">}}-el8-aarch64.tar.gz
    ```

1. Extract the package and then change directories to the YugabyteDB home.

    ```sh
    tar xvfz yugabyte-{{< yb-version version="v2.20" format="build">}}-linux-x86_64.tar.gz && cd yugabyte-{{< yb-version version="v2.20">}}/
    ```

    Or:

    ```sh
    tar xvfz yugabyte-{{< yb-version version="v2.20" format="build">}}-el8-aarch64.tar.gz && cd yugabyte-{{< yb-version version="v2.20">}}/
    ```

## Configure YugabyteDB

To configure YugabyteDB, run the following shell script:

```sh
./bin/post_install.sh
```
