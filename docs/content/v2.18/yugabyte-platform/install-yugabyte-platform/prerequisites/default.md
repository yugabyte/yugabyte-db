---
title: Prerequisites - Replicated
headerTitle: Prerequisites for YBA
linkTitle: YBA prerequisites
description: Prerequisites for installing YugabyteDB Anywhere using Replicated.
headContent: What you need to install YugabyteDB Anywhere
menu:
  v2.18_yugabyte-platform:
    identifier: prerequisites
    parent: install-yugabyte-platform
    weight: 30
type: docs
---

You can install YugabyteDB Anywhere (YBA) using the following methods:

| Method | Using | Use If |
| :--- | :--- | :--- |
| YBA Installer | yba-ctl CLI | You are performing a new installation. |
| Replicated | Docker containers | Your installation already uses Replicated. |
| Kubernetes | Helm chart | You're deploying in Kubernetes. |

All installation methods support installing YBA with and without (airgapped) Internet connectivity.

Licensing (such as a license file in the case of Replicated, or appropriate repository access in the case of Kubernetes) may be required prior to installation.  Contact {{% support-platform %}} for assistance.

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../installer/" class="nav-link">
      <i class="fa-solid fa-building" aria-hidden="true"></i>YBA Installer</a>
  </li>
  <li>
    <a href="../default/" class="nav-link active">
      <i class="fa-solid fa-cloud"></i>Replicated</a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

</ul>

## Supported Linux distributions

YugabyteDB Anywhere is supported on all Linux distributions that Replicated supports. This includes, but is not limited to the following:

- CentOS 7
- AlmaLinux 8
- AlmaLinux 9
- Ubuntu 18
- Ubuntu 20
- RedHat Enterprise Linux 7
- RedHat Enterprise Linux 8

## Hardware requirements

A node running YugabyteDB Anywhere is expected to meet the following requirements:

- 4 cores
- 8 GB memory
- 265+ GB disk space (see following table)

| Path | Usage | Free Space Required |
| :--- | :--- | :--- |
| / | Assuming /opt/yugabyte shares a file system with / | 50 GB |
| /tmp | Used during Install and Upgrade | 10 GB<sup>1</sup> |
| /opt/yugabyte | Database configuration and Prometheus logs | 150+ GB<sup>1,2</sup> |
| /var/lib/docker | Running YBA components | 40 GB<sup>1</sup> |
| /var/lib/replicated | Images and staging | 15 GB<sup>1</sup> |

<sup>1</sup> Where two or more of these paths share a file system, the free space required on that file system is the sum of the free space requirements.

<sup>2</sup> YugabyteDB Anywhere installations managing many nodes, or universes with many tables may require more than 150 GB to retain Prometheus logs, depending on retention. Installations managing fewer objects may need as little as 50-100 GB for /opt/yugabyte.

## Prepare the host

YugabyteDB Anywhere uses [Replicated scheduler](https://www.replicated.com/) for software distribution and container management. You need to ensure that the host can pull containers from the [Replicated Docker Registries](https://help.replicated.com/docs/native/getting-started/docker-registries/).

Replicated installs a compatible Docker version if it is not pre-installed on the host. The currently supported Docker version is 20.10.n.

Installing on airgapped hosts requires additional configurations, as described in [Airgapped hosts](#airgapped-hosts).

### Airgapped hosts

Installing YugabyteDB Anywhere on airgapped hosts (without access to any Internet traffic (inbound or outbound)) requires the following additional configuration:

- Whitelist endpoints: To install Replicated and YugabyteDB Anywhere on a host with no Internet connectivity, you have to first download the binaries on a computer that has Internet connectivity, and then copy the files over to the appropriate host. In case of restricted connectivity, whitelist the following endpoints to ensure that they are accessible from the host marked for installation:

  - `https://downloads.yugabyte.com`
  - `https://download.docker.com`

- Ensure that Docker Engine version 20.10.n is available. If it is not installed, you need to follow the procedure described in [Installing Docker in airgapped](https://community.replicated.com/t/installing-docker-in-airgapped-environments/81).

    If you want to set up Docker on Amazon Linux OS, perform the following:

    1. Install Docker using the following commands:

        ```sh
        sudo yum install docker
        sudo systemctl daemon-reload
        ```

    1. Start Docker using the following command:

        ```sh
        sudo systemctl start docker.service
        ```

- Ensure that the following ports are open on the YugabyteDB Anywhere host:
  - `8800` – HTTP access to the Replicated UI
  - `80` – HTTP access to the YugabyteDB Anywhere UI
  - `22` – SSH
- Obtain the YugabyteDB Anywhere airgapped install package. Contact {{% support-platform %}} for more information.
- Sign the Yugabyte license agreement. Contact {{% support-platform %}} for more information.
