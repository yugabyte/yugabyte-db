---
title: YugabyteDB Anywhere networking requirements
headerTitle: Prerequisites to deploy YBA on a VM
linkTitle: Server for YBA
description: Prerequisites for installing YugabyteDB Anywhere.
headContent: Prepare a VM for YugabyteDB Anywhere
menu:
  preview_yugabyte-platform:
    identifier: server-yba
    parent: prepare
    weight: 30
type: docs
rightNav:
  hideH4: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../server-yba/" class="nav-link active">
      <i class="fa-solid fa-building"></i>On-premises and public clouds</a>
  </li>

  <li>
    <a href="../server-yba-kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

</ul>

When installing YugabyteDB Anywhere (YBA) on-premises or on a public cloud (AWS, GCP, and Azure), you install YBA onto a virtual machine server with a Linux operating system (OS).

To meet the hardware and software prerequisites for YBA, create a VM that satisfies the following requirements.

## Hardware requirements

The server for YugabyteDB Anywhere must meet the following hardware requirements:

- 4 cores
- 8 GB memory
- 215 GB disk space
- x86 (Intel, AMD) architecture

## Software requirements

YugabyteDB Anywhere has the following software requirements:

- x86 Linux operating system
- License
- Python 3.8-3.11
- Sudo root permissions for installation

### Linux OS

You can install YugabyteDB Anywhere onto any [x86 Linux operating system](../../../reference/configuration/operating-systems/) supported by YugabyteDB. YugabyteDB Anywhere can't be installed on ARM architectures (but can be used to deploy universes to ARM-based nodes).

YugabyteDB Anywhere may also work on other Linux distributions; contact your YugabyteDB support representative if you need added support.

### License

You need your license file to install YugabyteDB Anywhere. Contact {{% support-platform %}} for assistance.

### Python

Python v3.8 to v3.11 must be pre-installed.

Both python and python3 must symbolically link to Python 3. One way to achieve this is to use alternatives. For example:

```sh
sudo yum install @python38 -y
sudo alternatives --config python
# choose Python 3.8 from list

sudo alternatives --config python3
# choose Python 3.8 from list

python -V
# output: Python 3.8.16

python3 -V
# output: Python 3.8.16
```

#### Permissions

You need sudo root permissions for installation for production use. (Without sudo root permissions, you can install YBA for non-production use.)

If your sudo permissions are limited, add the following commands to the sudoers file:

```sh
/usr/bin/yba-ctl clean
<path-to-yba-ctl>/yba-ctl clean
/usr/bin/yba-ctl createBackup
<path-to-yba-ctl>/yba-ctl generate-config
/usr/bin/yba-ctl help
<path-to-yba-ctl>/yba-ctl help
<path-to-yba-ctl>/yba-ctl install
/usr/bin/yba-ctl license
<path-to-yba-ctl>/yba-ctl license
/usr/bin/yba-ctl log-bundle
/usr/bin/yba-ctl preflight
<path-to-yba-ctl>/yba-ctl preflight
/usr/bin/yba-ctl reconfigure
<path-to-yba-ctl>/yba-ctl replicated-migrate
/usr/bin/yba-ctl replicated-migrate
/usr/bin/yba-ctl restart
<path-to-yba-ctl>/yba-ctl restart
/usr/bin/yba-ctl restoreBackup
/usr/bin/yba-ctl start
<path-to-yba-ctl>/yba-ctl start
/usr/bin/yba-ctl status
<path-to-yba-ctl>/yba-ctl status
/usr/bin/yba-ctl stop
<path-to-yba-ctl>/yba-ctl stop
<path-to-yba-ctl>/yba-ctl upgrade
/usr/bin/yba-ctl version
<path-to-yba-ctl>/yba-ctl version
```

Where path-to-yb-ctl is the location where you will [install YBA Installer](../../install-yugabyte-platform/install-software/installer/#download-yba-installer).

### High availability deployments

If you plan to deploy YBA in active-passive [high-availability](../../administer-yugabyte-platform/high-availability/) mode, you need two VMs (identical in hardware and software); one for the active YBA instance, and another for the standby YBA instance.
