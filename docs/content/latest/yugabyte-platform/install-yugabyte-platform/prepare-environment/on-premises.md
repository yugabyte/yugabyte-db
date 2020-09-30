---
title: Prepare the on-premises environment
headerTitle: Prepare the on-premises environment
linkTitle: Prepare the environment
description: Prepare the on-premises environment for Yugabyte Platform.
aliases:
  - /latest/deploy/enterprise-edition/prepare-environment/
  - /latest/yugabyte-platform/deploy/prepare-environment/
menu:
  latest:
    parent: install-yugabyte-platform
    identifier: prepare-environment-5-on-premises
    weight: 55
isTocNested: false
showAsideToc: false
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/prepare-environment/aws" class="nav-link">
      <i class="fab fa-aws" aria-hidden="true"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/prepare-environment/gcp" class="nav-link">
       <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

<!--

  <li>
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/prepare-environment/azure" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      Azure
    </a>
  </li>

-->

  <li>
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/prepare-environment/kubernetes" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/prepare-environment/on-premises" class="nav-link active">
      <i class="fas fa-building" aria-hidden="true"></i>
      On-premises
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/prepare-environment/air-gapped" class="nav-link">
      <i class="fas fa-unlinked" aria-hidden="true"></i>
      Airgapped
    </a>
  </li>

</ul>

<!--
## Prerequisites

For Yugabyte Platform, see [Prerequisites](../../../install-yugabyte-platform/prerequisites).

-->

## Prepare the Yugabyte Platform host for on-premises

Create a user on the YB Platform VM that has passwordless sudo privileges user on yugabyte:

```sh
$ sudo groupadd yugabyte
$ sudo useradd -m -s /bin/bash -g yugabyte yugabyte
$ sudo passwd yugabyte
$ sudo usermod -aG wheel yugabyte
```

Setup a new user account that has ssh access to the VM. The user being created in this case is yugabyte with sudo privileges on the node ideally.

Do the following as the new user (yugabyte) to enable passwordless ssh

```sh
$ mkdir .ssh
$ chmod 700 .ssh
$ touch .ssh/authorized_keys
$ chmod 600 .ssh/authorized_keys
```

Make custom directory for airgap install, here the /data directory is being used

```sh
$ sudo mkdir /data
$ sudo chown yugabyte:yugabyte /data
```

Install Platform according to the instructions on the Yugabyte Platform deployment docs page.