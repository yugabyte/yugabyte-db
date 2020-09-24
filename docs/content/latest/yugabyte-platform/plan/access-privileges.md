---
title: Prepare access and privileges
headerTitle: Prepare access and privileges
linkTitle: Prepare access and privilege
description: Access and privilege requirements for the Yugabyte Platform and YugabyteDB data nodes.
menu:
  latest:
    parent: plan-yugabyte-platform
    identifier: access-privileges
    weight: 627
type: page
isTocNested: true
showAsideToc: true
---


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
