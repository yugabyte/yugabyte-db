---
title: Prerequisites - YBA Installer
headerTitle: Prerequisites for YBA
linkTitle: YBA prerequisites
description: Prerequisites for installing YugabyteDB Anywhere using YBA Installer
headContent: What you need to install YugabyteDB Anywhere
menu:
  preview_yugabyte-platform:
    identifier: prerequisites-installer
    parent: install-yugabyte-platform
    weight: 20
type: docs
---

You can install YugabyteDB Anywhere (YBA) using the following methods:

| Method | Using | Use If |
| :--- | :--- | :--- |
| YBA Installer | yba-ctl CLI | You are performing a new installation.<br>You are ready to migrate from a Replicated installation. |
| Replicated | Docker containers | Your installation already uses Replicated. |
| Kubernetes | Helm chart | You're deploying in Kubernetes. |

All installation methods support installing YBA with and without (airgapped) Internet connectivity.

Licensing (such as a license file in the case of YBA Installer or Replicated, or appropriate repository access in the case of Kubernetes) may be required prior to installation. Contact {{% support-platform %}} for assistance.

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../installer/" class="nav-link active">
      <i class="fa-solid fa-building" aria-hidden="true"></i>YBA Installer</a>
  </li>

  <li>
    <a href="../default/" class="nav-link">
      <i class="fa-solid fa-cloud"></i>Replicated</a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

</ul>

## Software requirements

- You can install YugabyteDB Anywhere onto any [x86 Linux operating system](../../../../reference/configuration/operating-systems/) supported by YugabyteDB. YugabyteDB Anywhere doesn't support ARM architectures (but can be used to deploy universes to ARM-based nodes).

    YugabyteDB Anywhere may also work on other Linux distributions; contact your YugabyteDB support representative if you need added support.

- Python v3.8 to 3.11 must be installed, and both `python` and `python3` must point to Python 3. One way to achieve this is to use `alternatives`. For example:

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

## Permissions

For production deployments, sudo permissions are required for some YBA Installer commands. (You can use YBA Installer without sudo access, but this is not recommended for production; refer to [Non-sudo installation](../../install-software/installer/#non-sudo-installation).)

If your sudo permissions are limited, add the following to the allowed list for root in the sudoers file:

```sh
/bin/mv, /usr/bin/find, /opt/yugabyte/software/*/pgsql/bin/createdb, /opt/yugabyte/software/*/pgsql/bin/initdb
```

In addition, add the following commands to the sudoers file:

```sh
/usr/bin/yba-ctl clean
./yba-ctl clean
/usr/bin/yba-ctl createBackup
./yba-ctl generate-config
/usr/bin/yba-ctl help
./yba-ctl help
./yba-ctl install
/usr/bin/yba-ctl license
./yba-ctl license
/usr/bin/yba-ctl log-bundle
/usr/bin/yba-ctl preflight
./yba-ctl preflight
/usr/bin/yba-ctl reconfigure
./yba-ctl replicated-migrate
/usr/bin/yba-ctl replicated-migrate
/usr/bin/yba-ctl restart
./yba-ctl restart
/usr/bin/yba-ctl restoreBackup
/usr/bin/yba-ctl start
./yba-ctl start
/usr/bin/yba-ctl status
./yba-ctl status
/usr/bin/yba-ctl stop
./yba-ctl stop
./yba-ctl upgrade
/usr/bin/yba-ctl version
./yba-ctl version
```

## Hardware requirements

A node running YugabyteDB Anywhere is expected to meet the following requirements:

- 4 cores
- 8 GB memory
- 215 GB disk space
- x86 architecture

## Ports

Ensure that the following ports are available:

- 443 (HTTPS)
- 5432 (PostgreSQL)
- 9090 (Prometheus)

These are configurable. If custom ports are used, those must be available instead.

For more information on ports used by YugabyteDB, refer to [Default ports](../../../../reference/configuration/default-ports).
