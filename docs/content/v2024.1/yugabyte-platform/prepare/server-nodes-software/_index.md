---
title: YugabyteDB Anywhere node software requirements
headerTitle: Software requirements for nodes
linkTitle: Software requirements
description: Software prerequisites for nodes running YugabyteDB.
headContent: Operating system and additional software required for YugabyteDB
menu:
  v2024.1_yugabyte-platform:
    identifier: server-nodes-software
    parent: server-nodes
    weight: 20
type: indexpage
showRightNav: true
---

The Linux OS and other software components on each database cluster node must meet the following minimum software requirements.

Depending on the [provider type](../../yba-overview/#provider-configurations) and permissions you grant, you may have to install all of these requirements manually, or YugabyteDB Anywhere will install it all automatically.

{{< warning title="Using disk encryption software with YugabyteDB" >}}
If you are using third party disk encryption software (such as Vormetric or CipherTrust), the disk encryption service must be up and running on the node before starting any YugabyteDB services. If YugabyteDB processes start _before_ the encryption service, restarting an already encrypted node can result in data corruption.

To avoid problems, [pause the universe](../../manage-deployments/delete-universe/#pause-a-universe) _before_ enabling or disabling the disk encryption service on universe nodes.
{{< /warning >}}

### Linux OS

YugabyteDB Anywhere supports deploying YugabyteDB on a variety of [operating systems](../../../reference/configuration/operating-systems/).

AlmaLinux OS 8 disk images are used by default, but you can specify a custom disk image and OS.

### Additional software

YugabyteDB Anywhere requires the following additional software to be pre-installed on nodes:

- OpenSSH Server. Allowing SSH is optional. Using SSH is required in some [legacy on-premises deployment](../server-nodes-software/software-on-prem-legacy/) approaches. [Tectia SSH](../../create-deployments/connect-to-universe/#enable-tectia-ssh) is also supported.
- tar
- unzip
- policycoreutils-python-utils

#### Python

Install Python 3.8 on the nodes. (If you are using [Legacy on-premises provisioning](software-on-prem-legacy/), Python 3.5-3.8 is supported, and 3.6 is recommended.)

Install the Python SELinux package corresponding to your version of Python. You can use pip to do this. Ensure the version of pip matches the version of Python.

For example, you can install Python as follows:

```sh
sudo yum install python38
sudo pip3.8 install selinux
sudo ln -s /usr/bin/python3.8 /usr/bin/python
sudo rm /usr/bin/python3
sudo ln -s /usr/bin/python3.8 /usr/bin/python3
python3 -c "import selinux; import sys; print(sys.version)"
```

```output
> 3.8.19 (main, Sep 11 2024, 00:00:00)
> [GCC 11.5.0 20240719 (Red Hat 11.5.0-2)]
```

Alternately, if you are using the default version of python3, you might be able to install the python3-libselinux package.

### Additional software for airgapped deployment

Additionally, if not connected to the public Internet (that is, airgapped); and not connected to a local Yum repository that contains the [additional software](#additional-software), database cluster nodes must also have the following additional software pre-installed:

- libcgroup and libcgroup-tools (for planned future use of the cgconfig service, for cgroups; can be omitted for YBA versions earlier than v2024.1)
- rsync, openssl (all linux)
- semanage-utils (for Debian-based systems)
- glibc-locale-source, glibc-langpack-en
- libatomic (for Redhat-based aarch64)
- libatomic1, libncurses6 (for Debian-based aarch64)
- chrony (for time synchronization). When using a Public Cloud Provider, chrony is the only choice. When using an On-Premises provider, chrony is recommended, but ntpd is also an alternative option.
