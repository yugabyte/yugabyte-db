---
title: Prepare the on-premises environment
headerTitle: Prepare the on-premises environment
linkTitle: 1. Prepare the environment
description: Prepare the on-premises environment for Yugabyte Platform.
aliases:
  - /latest/deploy/enterprise-edition/prepare-environment/
  - /latest/yugabyte-platform/deploy/prepare-environment/
menu:
  latest:
    identifier: prepare-environment-5-on-premises
    parent: prepare-environment
    weight: 112
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="/latest/yugabyte-platform/install-yp/prepare-environment/aws" class="nav-link">
      <i class="fab fa-aws" aria-hidden="true"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yp/prepare-environment/gcp" class="nav-link">
       <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yp/prepare-environment/azure" class="nav-link">
      <i class="icon azure" aria-hidden="true"></i>
      Azure
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yp/prepare-environment/kubernetes" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yp/prepare-environment/on-premises" class="nav-link active">
      <i class="fas fa-building" aria-hidden="true"></i>
      On-premises
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yp/prepare-environment/air-gapped" class="nav-link">
      <i class="fas fa-unlinked aria-hidden="true"></i>
      Air-gapped
    </a>
  </li>

</ul>

A dedicated host or virtual machine (VM) is required to run the Yugabyte Platform server. For more details, see this faq. This page highlights the basic setup needed in order to install Yugabyte Platform.

## Prerequisites

For Yugabyte Platform, see [Review system requirements](../../../plan/system-reqs-yp).

## Install Yugabyte Platform on a VM

Requirements for YugabyteDB nodes

Create a user on the YB Platform VM that has passwordless sudo privileges user on yw:
$ sudo groupadd yw
$ sudo useradd -m -s /bin/bash -g yw yw
$ sudo passwd yw
$ sudo usermod -aG wheel yw

Setup a new user account that has ssh access to the VM. The user being created in this case is yw with sudo privileges on the node ideally.

Do the following as the new user (yw) to enable passwordless ssh
$ mkdir .ssh
$ chmod 700 .ssh
$ touch .ssh/authorized_keys
$ chmod 600 .ssh/authorized_keys

Make custom directory for airgap install, here the /data directory is being used
$ sudo mkdir /data
$ sudo chown yw:yw /data

Install Platform according to the instructions on the Yugabyte Platform deployment docs page.
