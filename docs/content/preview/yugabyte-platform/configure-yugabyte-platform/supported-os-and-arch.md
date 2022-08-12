---
title: Supported operating systems and architectures
headerTitle: Supported operating systems and architectures
linkTitle: OS support
description: List of operating systems and architectures supported by YugabyteDB Anywhere
menu:
  preview_yugabyte-platform:
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

### Python 3

Python 3 is required. If you're using YugabyteDB Anywhere to provision nodes in public clouds, be sure the custom AMI you plan to use has Python 3 installed.

### Oracle Linux notes

Oracle Linux 8 assumes that either `gtar` or `gunzip` is present on the host AMI, and uses the `firewall-cmd` client to set default target ACCEPT.

YugabyteDB Anywhere support for Oracle Linux 8 has the following limitations:

* Only the Red Hat Linux-compatible kernel is supported, to allow port changing. The Unbreakable Enterprise Kernel (UEK) isn't supported.
* Systemd services aren't supported.
