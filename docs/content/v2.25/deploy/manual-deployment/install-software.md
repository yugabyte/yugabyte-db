---
title: Manual software installation
headerTitle: 2. Install software
linkTitle: 2. Install software
description: How to manually install YugabyteDB database on each node.
menu:
  v2.25:
    identifier: deploy-manual-deployment-install-software
    parent: deploy-manual-deployment
    weight: 612
type: docs
---

Installing YugabyteDB involves completing prerequisites and downloading the YugabyteDB package.

## Prerequisites

{{% readfile "/stable/quick-start/include-prerequisites-linux.md" %}}

### Using disk encryption software with YugabyteDB

If you are using third party disk encryption software, such as Vormetric or CipherTrust, the disk encryption service must be up and running on the node before starting any YugabyteDB services. If YugabyteDB processes start _before_ the encryption service, restarting an already encrypted node can result in data corruption.

To avoid issues, stop YugabyteDB services on the node _before_ enabling or disabling the disk encryption service.

## Download YugabyteDB

The following instructions are for downloading the latest stable release of YugabyteDB, which is recommended for production deployments. For other versions, see [Releases](/preview/releases/).

{{<note title="Which release should I use?">}}
For production deployments, install a stable release.

Preview releases are recommended for development and testing only, and are not supported for production deployments. There is currently no migration path from a preview release to a stable release.
{{</note>}}

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
