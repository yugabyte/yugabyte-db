---
title: Prerequisites - Installer
headerTitle: Prerequisites - Installer
linkTitle: Prerequisites
description: Prerequisites for installing YugabyteDB Anywhere using the installer
menu:
  preview_yugabyte-platform:
    identifier: prerequisites-installer
    parent: install-yugabyte-platform
    weight: 30
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../default/" class="nav-link">
      <i class="fa-solid fa-cloud"></i>Default</a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

  <li>
    <a href="../installer/" class="nav-link active">
      <i class="fa-solid fa-building" aria-hidden="true"></i>Installer</a>
  </li>

</ul>

YugabyteDB Anywhere first needs to be installed on a host computer, and then you configure YugabyteDB Anywhere to work in your on-premises private cloud or in a public cloud environment. In a public cloud environment, YugabyteDB Anywhere spawns instances for starting a YugabyteDB universe. In a private cloud environment, you use YugabyteDB Anywhere to add nodes in which you want to be in the YugabyteDB universe. To manage these nodes, YugabyteDB Anywhere requires SSH access to each of the nodes.

## Supported Linux distributions

You can install YugabyteDB Anywhere using the Installer on the following Linux distributions:

- CentOS (default)
- RHEL 7 and later
- Ubuntu 18 and 20
- SUSE

Python 3 must be installed.

## Hardware requirements

A node running YugabyteDB Anywhere is expected to meet the following requirements:

- 4 cores (minimum) or 8 cores (recommended)
- 8 GB RAM (minimum) or 10 GB RAM (recommended)
- 200 GB SSD disk or more
- x86-64 CPU architecture

## Other

- Ensure that the following ports are available:
  - 443
  - 5432
  - 9080

These are configurable. If custom ports are used, those must be available instead.
