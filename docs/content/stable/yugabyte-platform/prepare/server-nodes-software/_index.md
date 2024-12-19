---
title: YugabyteDB Anywhere node software requirements
headerTitle: Software requirements for nodes
linkTitle: Software requirements
description: Software prerequisites for nodes running YugabyteDB.
headContent: Operating system and additional software required for YugabyteDB
menu:
  stable_yugabyte-platform:
    identifier: server-nodes-software
    parent: server-nodes
    weight: 20
type: indexpage
---

The Linux OS and other software components on each database cluster node must meet the following minimum software requirements.

Depending on the [provider type](../../yba-overview/#provider-configurations) and permissions you grant, you may have to install all of these requirements manually, or YugabyteDB Anywhere will install it all automatically.

{{< warning title="Using disk encryption software with YugabyteDB" >}}
If you are using third party disk encryption software, such as Vormetric or CipherTrust, the disk encryption service must be up and running on the node before starting any YugabyteDB services. If YugabyteDB processes start _before_ the encryption service, restarting an already encrypted node can result in data corruption.

To avoid problems, [pause the universe](../../manage-deployments/delete-universe/#pause-a-universe) _before_ enabling or disabling the disk encryption service on universe nodes.
{{< /warning >}}

### Linux OS

YugabyteDB Anywhere supports deploying YugabyteDB on a variety of [operating systems](../../../reference/configuration/operating-systems/).

AlmaLinux OS 8 disk images are used by default, but you can specify a custom disk image and OS.

### Additional software

YugabyteDB Anywhere requires the following additional software to be pre-installed on nodes:

- Python 3.6-3.8
- Install the python selinux package corresponding to your version of python. For example, using pip, you can install as follows:

  `python3 -m pip install selinux`

  Alternately, if you are using the default version of python3, you might be able to install the python3-libselinux package.

- OpenSSH Server. Allowing SSH is recommended but optional. Using SSH can be skipped in some on-premises deployment approaches; all other workflows require it. [Tectia SSH](../../create-deployments/connect-to-universe/#enable-tectia-ssh) is also supported.
- tar
- unzip
- policycoreutils-python-utils

### Additional software for airgapped deployment

Additionally, if not connected to the public Internet (that is, airgapped); and not connected to a local Yum repository that contains the [additional software](#additional-software), database cluster nodes must also have the following additional software pre-installed:

- libcgroup and libcgroup-tools (for planned future use of the cgconfig service, for cgroups; can be omitted for YBA versions earlier than v2024.1)
- rsync, openssl (all linux)
- semanage-utils (for Debian-based systems)
- glibc-locale-source, glibc-langpack-en
- libatomic (for Redhat-based aarch64)
- libatomic1, libncurses6 (for Debian-based aarch64)
- chrony (for time synchronization). When using a Public Cloud Provider, chrony is the only choice. When using an On-Premises provider, chrony is recommended; ntpd and systemd-timesyncd are also supported.
