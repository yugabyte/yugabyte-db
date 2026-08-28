---
title: Provision servers for cloud provider configuration database nodes
headerTitle: Software requirements for cloud provider configuration database nodes
linkTitle: Cloud provider
description: Prepare a VM for deploying universes using cloud provider configurations.
headContent: Prepare a VM for deploying universes using a cloud provider configuration
menu:
  stable_yugabyte-platform:
    identifier: software-cloud-provider
    parent: server-nodes-software
    weight: 10
type: docs
---

When deploying universes using a public [cloud provider configuration](../../../yba-overview/#provider-configurations) (AWS, GCP, or Azure), YugabyteDB Anywhere (YBA) creates cloud VMs for database nodes directly.

You have two options for provisioning the operating system for database nodes:

- Use a default (YBA-managed) Linux version (and disk image).
- Specify a custom Linux version (and disk image).

Using a YBA-managed Linux version requires connectivity from the database nodes to the public Internet. If you lack such connectivity, you will need to use a custom Linux version.

## YBA-managed Linux version

For YBA-managed Linux version, YBA manages the creation and provisioning of database nodes, including installing the disk image, configuring the Linux OS, and installing the additional software.

You can proceed directly to installing YBA, creating your cloud provider configuration, and creating universes. YBA-managed Linux versions use the image's default login user: `ec2-user` on AWS, `centos` on GCP and Azure.

## Custom Linux version with Internet connectivity

If you choose to provide your own custom Linux version and your VMs have connectivity to the public Internet, you must provide to YBA a Linux OS disk image with the following pre-installed:

- [Supported Linux OS](../#linux-os) with an SSH-enabled user that has passwordless sudo. The user must not be named `yugabyte` (YBA creates that account during provisioning). For standard cloud images, use the image's default login user (the user the cloud injects the SSH key into), for example `ec2-user`, `centos`, or `ubuntu`. YBA uses this user to configure the OS, including creating the `yugabyte` user and updating ulimits.
- [Additional software](../#additional-software)

Take the time now to prepare the Linux disk image.

- Save the SSH user name and the SSH private key PEM file.
- Save the disk image IDs for later when installing and configuring YBA.

| Save for later | To configure |
| :--- | :--- |
| SSH user name | [Linux version catalog](../../../configure-yugabyte-platform/aws/#linux-version-catalog) |
| SSH private key PEM file | [SSH Key Pairs](../../../configure-yugabyte-platform/aws/#ssh-key-pairs) |
| Disk image IDs | [Linux version catalog](../../../configure-yugabyte-platform/aws/#linux-version-catalog) |

## Custom Linux version without Internet connectivity

If you choose to provide your own custom Linux version and your VMs don't have connectivity to the public Internet, you must provide to YBA a Linux OS disk image with the following pre-installed:

- [Supported Linux OS](../#linux-os) with an SSH-enabled user that has passwordless sudo. The user must not be named `yugabyte` (YBA creates that account during provisioning). For standard cloud images, use the image's default login user (the user the cloud injects the SSH key into), for example `ec2-user`, `centos`, or `ubuntu`. YBA uses this user to configure the OS, including creating the `yugabyte` user and updating ulimits.
- [Additional software](../#additional-software)
- [Additional software for airgapped](../#additional-software-for-airgapped-deployment)

Take the time now to prepare the Linux disk image.

- Save the SSH user name and the SSH private key PEM file.
- Save the disk image IDs for later when installing and configuring YBA.

| Save for later | To configure |
| :--- | :--- |
| SSH user name | [Linux version catalog](../../../configure-yugabyte-platform/aws/#linux-version-catalog) |
| SSH private key PEM file | [SSH Key Pairs](../../../configure-yugabyte-platform/aws/#ssh-key-pairs) |
| Disk image IDs | [Linux version catalog](../../../configure-yugabyte-platform/aws/#linux-version-catalog) |
