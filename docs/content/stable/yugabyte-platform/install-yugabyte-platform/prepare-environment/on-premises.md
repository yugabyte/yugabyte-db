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

</ul>

<!--
## Prerequisites

For Yugabyte Platform, see [Prerequisites](../../../install-yugabyte-platform/prerequisites).

-->

Create a user on the Yugabyte Platform VM that has passwordless `sudo` privileges user on yugabyte:

```sh
$ sudo groupadd yugabyte
$ sudo useradd -m -s /bin/bash -g yugabyte yugabyte
$ sudo passwd yugabyte
$ sudo usermod -aG wheel yugabyte
```

Set up a new user account that has SSH access to the VM. The user created here is `yugabyte` with `sudo` privileges on the node.

To enable passwordless SSH, do the following as the new user (`yugabyte`):

```sh
$ mkdir .ssh
$ chmod 700 .ssh
$ touch .ssh/authorized_keys
$ chmod 600 .ssh/authorized_keys
```

Make a custom directory for your airgapped installation. In the following example, the `/data` directory is being used.

```sh
$ sudo mkdir /data
$ sudo chown yugabyte:yugabyte /data
```
