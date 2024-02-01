---
title: Supported operating systems and architectures for YugabyteDB nodes
headerTitle: Node prerequisites
linkTitle: Node prerequisites
description: Operating systems and architectures supported by YugabyteDB Anywhere for deploying YugabyteDB
headcontent: Prerequisites for YugabyteDB universe nodes
menu:
  stable_yugabyte-platform:
    identifier: supported-os-and-arch
    parent: configure-yugabyte-platform
    weight: 6
type: docs
---

Using YugabyteDB Anywhere (YBA), you can deploy YugabyteDB universes on nodes with the following architectures and operating systems.

## Supported operating systems and CPU architectures

YBA supports deploying YugabyteDB on both x86 and ARM (aarch64) architecture-based hardware.

YBA supports deploying YugabyteDB on a variety of [operating systems](../../../reference/configuration/operating-systems/#yugabytedb-anywhere). AlmaLinux OS 9 is used by default.

### Requirements for all OSes

Python 3 is required. If you're using YugabyteDB Anywhere to provision nodes in public clouds, be sure the custom AMI you plan to use has Python 3 installed.

The host AMI must have `gtar` and `zipinfo` installed.

### Oracle Linux and AlmaLinux notes

YBA support for Oracle Linux 8 and AlmaLinux OS 8 has the following limitations:

* Oracle Linux 8 uses the `firewall-cmd` client to set default target `ACCEPT`.
* On Oracle Linux 8, only the Red Hat Linux-compatible kernel is supported, to allow port changing. The Unbreakable Enterprise Kernel (UEK) is not supported.
* Systemd services are only supported in YugabyteDB Anywhere 2.15.1 and later versions.
