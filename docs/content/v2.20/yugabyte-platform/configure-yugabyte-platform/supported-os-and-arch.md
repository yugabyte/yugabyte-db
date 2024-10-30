---
title: Supported operating systems and architectures for YugabyteDB nodes
headerTitle: Node prerequisites
linkTitle: Node prerequisites
description: Operating systems and architectures supported by YugabyteDB Anywhere for deploying YugabyteDB
headcontent: Prerequisites for YugabyteDB universe nodes
menu:
  v2.20_yugabyte-platform:
    identifier: supported-os-and-arch
    parent: configure-yugabyte-platform
    weight: 6
type: docs
---

Using YugabyteDB Anywhere (YBA), you can deploy YugabyteDB universes on nodes with the following architectures and operating systems.

-> For information on prerequisites for the node running YBA, refer to [Prerequisites for YBA](../../install-yugabyte-platform/prerequisites/installer/).

## Supported operating systems and CPU architectures

YBA supports deploying YugabyteDB on both x86 and ARM (aarch64) architecture-based hardware.

YBA supports deploying YugabyteDB on a variety of [operating systems](../../../reference/configuration/operating-systems/). AlmaLinux OS 8 is used by default.

### Requirements for all OSes

Verify that Python 3.5-3.8 is installed. v3.6 is recommended.

In case there is more than one Python 3 version installed, ensure that `python3` refers to the right one. For example:

```sh
sudo alternatives --set python3 /usr/bin/python3.6
sudo alternatives --display python3
python3 -V
```

If you are using Python later than v3.6, install the [selinux](https://pypi.org/project/selinux/) package corresponding to your version of python. For example, using [pip](https://pip.pypa.io/en/stable/installation/), you can install as follows:

```sh
python3 -m pip install selinux
```

Refer to [Ansible playbook fails with libselinux-python aren't installed on RHEL8](https://access.redhat.com/solutions/5674911) for more information.

If you are using Python later than v3.7, set the **Max Python Version (exclusive)** Global Configuration option to the python version. Refer to [Manage runtime configuration settings](../../administer-yugabyte-platform/manage-runtime-config/). Note that only a Super Admin user can modify Global configuration settings.

For more information on additional requirements for nodes for use in on-premises provider configurations, refer to [Manually provision on-premises nodes](../set-up-cloud-provider/on-premises-manual/).

### Oracle Linux and AlmaLinux notes

YBA support for Oracle Linux 8 and AlmaLinux OS 8 has the following limitations:

* Oracle Linux 8 uses the `firewall-cmd` client to set default target `ACCEPT`.
* On Oracle Linux 8, only the Red Hat Linux-compatible kernel is supported, to allow port changing. The Unbreakable Enterprise Kernel (UEK) is not supported.
* Systemd services are only supported in YBA 2.15.1 and later versions.
