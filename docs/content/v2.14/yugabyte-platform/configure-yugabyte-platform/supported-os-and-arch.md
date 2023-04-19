---
title: Supported operating systems and architectures
headerTitle: Supported operating systems and architectures
linkTitle: OS support
description: List of operating systems and architectures supported by YugabyteDB Anywhere
menu:
  v2.14_yugabyte-platform:
    identifier: supported-os-and-arch
    parent: configure-yugabyte-platform
    weight: 6
type: docs
---

## Supported CPU architectures

YugabyteDB Anywhere supports deploying YugabyteDB on both x86 and ARM (aarch64) architecture-based hardware.

## Supported operating systems

YugabyteDB Anywhere supports deploying YugabyteDB on the following operating systems:

* AlmaLinux OS 8
* CentOS 7
* Oracle Linux 8
* Ubuntu 18
* Ubuntu 20

### Requirements for all OSes

Python 3 is required. If you're using YugabyteDB Anywhere to provision nodes in public clouds, be sure the custom AMI you plan to use has Python 3 installed.

The host AMI must have `gtar` and `zipinfo` installed.

### Oracle Linux and AlmaLinux notes

YugabyteDB Anywhere support for Oracle Linux 8 and AlmaLinux OS 8 has the following limitations:

* Oracle Linux 8 uses the `firewall-cmd` client to set default target ACCEPT.
* On Oracle Linux 8, only the Red Hat Linux-compatible kernel is supported, to allow port changing. The Unbreakable Enterprise Kernel (UEK) isn't supported.
* Systemd services are only supported in YugabyteDB Anywhere 2.15.1 and above.
