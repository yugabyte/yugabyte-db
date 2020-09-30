---
title: Prepare nodes (on-prem)
headerTitle: Prepare nodes (on-prem)
linkTitle: Prepare nodes (on-prem)
description: Prepare YugabyteDB nodes for on-premises deployments.
menu:
  latest:
    identifier: prepare-on-prem-nodes
    parent: install-yugabyte-platform
    weight: 79
isTocNested: true
showAsideToc: true
---

For on-premises deployments of Yugabyte Platform, you need to import nodes which can be managed by Yugabyte Platform. This page outlines the steps required to prepare these YugabyteDB nodes for on-premises deployments.

{{< note title="Note" >}}

For airgapped, on-premises deployments (without Internet connectivity), additional steps are required as outlined below.

{{< /note >}}

Ensure that the YugabyteDB nodes conform to the requirements outlines in the [deployment checklist](../../../deploy/checklist/). This checklist also gives an idea of [recommended instance types across public clouds](../../../deploy/checklist/#running-on-public-clouds).

Install the prerequisites and verify the system resource limits as described in [system configuration](../../../deploy/manual-deployment/system-config).




## SSH user

Identify an SSH user or create an SSH  user. This user must have `sudo` privileges on each YugabyteDB node. 

To add a new SSH user, follow this procedure:

1. Add the `yw` group.

```sh
$ sudo groupadd yw
```

2. Add the SSH user `yw`.

```ssh
$ sudo useradd -m -s /bin/bash -g yw yw
```

3. Add a password for the user `yw`.

```sh
$ sudo passwd yw
```

4. Ensure the `/home/yw` home directory exists for this user. The directory should have been created automatrically as a result of the above steps.

5. Verify the `/home/yw` directory is owned by `yw:yw`.

## Access as `sudo`

1. Add the `yw` user to the sudo users file (`/etc/sudoers`) using the `visudo` command. 

2. Add the line below to the end of the file and then save your changes:

```sh
yw	ALL=(ALL)	NOPASSWD: ALL
```

## Passwordless access

1. Add the SSH keys to enable passwordless SSH as the `yw` user:

```sh
$ sudo mkdir /home/yw/.ssh
$ sudo chown yw:yw /home/yw/.ssh
$ sudo chmod 700 /home/yw/.ssh
$ sudo touch /home/yw/.ssh/authorized_keys
$ sudo chmod 600 /home/yw/.ssh/authorized_keys
$ sudo chown yw:yw /home/yw/.ssh/authorized_keys
```

2. Add the public key of the on-premises provider to authorized keys for this user. This is a public key derived from the provider private key created when installing the Yugabyte Platform:

```sh
$ ssh-keygen -y -f <private-key-file>.pem
```

3.Verify that you can `ssh` into this node (from your local machine, if node has a public address).

```ssh
$ ssh -i your_private_key.pem yw@node_ip
```
