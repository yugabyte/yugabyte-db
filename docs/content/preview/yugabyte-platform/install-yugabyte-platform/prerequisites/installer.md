---
title: Prerequisites - YBA Installer
headerTitle: Prerequisites for YBA
linkTitle: YBA prerequisites
description: Prerequisites for installing YugabyteDB Anywhere using YBA Installer
headContent: What you need to install YugabyteDB Anywhere
earlyAccess: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview_yugabyte-platform:
    identifier: prerequisites-installer
    parent: install-yugabyte-platform
    weight: 30
type: docs
---

You can install YugabyteDB Anywhere (YBA) using the following methods:

| Method | Using | Use If |
| :--- | :--- | :--- |
| Replicated | Docker containers | You're able to use Docker containers. |
| Kubernetes | Helm chart | You're deploying in Kubernetes. |
| YBA Installer | yba-ctl CLI | You can't use Docker containers.<br/>(Note: in Early Access, contact {{% support-platform %}}) |

All installation methods support installing YBA with and without (airgapped) Internet connectivity.

Licensing (such as a license file in the case of Replicated, or appropriate repository access in the case of Kubernetes) may be required prior to installation.  Contact {{% support-platform %}} for assistance.

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../default/" class="nav-link">
      <i class="fa-solid fa-cloud"></i>Replicated</a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

  <li>
    <a href="../installer/" class="nav-link active">
      <i class="fa-solid fa-building" aria-hidden="true"></i>YBA Installer</a>
  </li>

</ul>

## Supported Linux distributions

You can install YugabyteDB Anywhere using YBA Installer on the following Linux distributions:

- CentOS 7
- Alma Linux 8
- Alma Linux 9
- Ubuntu 18
- Ubuntu 20
- RedHat Enterprise Linux 7
- RedHat Enterprise Linux 8
- SUSE Linux Enterprise Server (SLES) 15 SP4 (Tech Preview)

YugabyteDB Anywhere may also work on other Linux distributions; contact your YugabyteDB support representative if you need added support.

## Software requirements

- Python 3 must be installed.

## Hardware requirements

A node running YugabyteDB Anywhere is expected to meet the following requirements:

- 4 cores
- 8 GB memory
- 215 GB disk space

## Other

Ensure that the following ports are available:

- 443 (HTTPS)
- 5432 (PostgreSQL)
- 9090 (Prometheus)

These are configurable. If custom ports are used, those must be available instead.

For more information on ports used by YugabyteDB, refer to [Default ports](../../../../reference/configuration/default-ports).
