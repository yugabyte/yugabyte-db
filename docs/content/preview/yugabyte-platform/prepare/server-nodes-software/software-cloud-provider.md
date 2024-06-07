---
title: YugabyteDB Anywhere node software requirements
headerTitle: Software requirements for cloud provider nodes
linkTitle: Cloud provider
description: Software requirements for cloud provider nodes.
headContent: Prepare a VM for deployment in a universe
menu:
  preview_yugabyte-platform:
    identifier: software-cloud-provider
    parent: server-nodes-software
    weight: 10
type: docs
---

When deploying database clusters using a public cloud provider (AWS, GCP, or Azure), YugabyteDB Anywhere creates cloud VMs directly.

You have two options for provisioning the operating system:

- Use a default (YBA-managed) Linux version (and disk image).
- Specify a custom Linux version (and disk image).

Using a YBA-managed Linux version requires connectivity from the database cluster nodes to the public Internet. If you lack such connectivity, you will need to use a custom Linux version.

## YBA-managed Linux version

For YBA-managed Linux version, YBA manages the creation and provisioning of database cluster nodes, including installing the disk image, configuring the Linux OS, and installing the additional software.

You can proceed directly to installing YBA, creating your provider, and creating universes.

## Custom Linux version with Internet connectivity

If you choose to provide your own custom Linux version and your VMs have connectivity to the public Internet, you must provide to YBA a Linux OS disk image with the following pre-installed:

- [Supported Linux OS](../#linux-os) with an SSH-enabled, root-privileged user. YBA uses this user to automatically perform additional Linux configuration, such as creating the `yugabyte` user, updating the file descriptor settings via ulimits, and so on.
- [Additional software](../#additional-software)

Take the time now to prepare the Linux disk image.

- Save the SSH-enabled, root-privileged user credentials (username and SSH Private Key Content PEM file).
- Save the disk image IDs for later when installing and configuring YBA.

| Save for later | To configure |
| :--- | :--- |
| SSH-enabled, root-privileged user name | [Cloud provider](../../../configure-yugabyte-platform/aws/) |
| SSH-enabled, root-privileged Private Key Content PEM file | [Cloud provider](../../../configure-yugabyte-platform/aws/) |
| Disk image IDs | [Cloud provider](../../../configure-yugabyte-platform/aws/) |

## Custom Linux version without Internet connectivity

If you choose to provide your own custom Linux version and your VMs don't have connectivity to the public Internet, you must provide to YBA a Linux OS disk image with the following pre-installed:

- [Supported Linux OS](../#linux-os) with an SSH-enabled, root-privileged user. YBA uses this user to automatically perform additional Linux configuration, such as creating the `yugabyte` user, updating the file descriptor settings via ulimits, and so on.
- [Additional software](../#additional-software)
- [Additional software for airgapped](../#additional-software-for-airgapped-deployment)

Take the time now to prepare the Linux disk image.

- Save the SSH-enabled, root-privileged user credentials (username and SSH Private Key Content PEM file).
- Save the disk image IDs for later when installing and configuring YBA.

| Save for later | To configure |
| :--- | :--- |
| SSH-enabled, root-privileged user name | [Cloud provider](../../../configure-yugabyte-platform/aws/) |
| SSH-enabled, root-privileged Private Key Content PEM file | [Cloud provider](../../../configure-yugabyte-platform/aws/) |
| Disk image IDs | [Cloud provider](../../../configure-yugabyte-platform/aws/) |
