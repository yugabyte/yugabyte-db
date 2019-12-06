---
title: Encryption at rest
linkTitle: Encryption at rest
description: Encryption at rest
headcontent: Enable encryption at rest with a user generated key
image: /images/section_icons/secure/prepare-nodes.png
aliases:
  - /secure/encryption-at-rest
menu:
  latest:
    identifier: encryption-at-rest
    parent: secure
    weight: 730
isTocNested: true
showAsideToc: true
---

Encryption at rest ensures that data stored on disk is protected. You can configure your YugabyteDB cluster with a *universe key* (a user-generated symmetric key), to perform cluster-wide encryption. Follow the steps below to enable or disable encryption at rest in a YugabyteDB cluster.


## Enabling encryption at rest

To enable encryption at rest, perform the following steps.

### Step 1. Generate universe key

To ensure encryption at rest across your YugabyteDB cluster, you need to create a universe key. This key can have length `32`, `40`, or `48`. Larger keys are slightly more secure with slightly worse performance. 

To generate a universe key, run the following on your local filesystem.

```sh
$ openssl rand -out universe_key_1 [ 32 | 40 | 48 ]

```

### Step 2. Copy universe key to YB-Master leader node

For this example, we assume a three-node RF=3 cluster with addresses `ip1` (`LEADER`), `ip2`, and `ip3`.

Copy the universe key onto the YB-Master leader node and it will be replicated to all YB-Master nodes in the cluster. In this example, the Secure Copy Protocol (SCP) command is used to copy the universe key from your local system to the YB-Master leader node.

```sh
$ scp -i <ssh_key> -P 54422 universe_key_1 ip1:/mnt/d0/yb-data/master/keys
```

{{< note title="Note" >}}

The universe key can be copied anywhere on the YB-Master node so that it can be loaded in-memory.

{{< /note >}}

### Step 3. Add the universe key to all YB-Master nodes

Run the following `yb-admin` [`add_universe_key_to_all_masters`](../../admin/yb-admin/#add-universe-key-to-all-masters) command.

```sh
yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 add_universe_key_to_all_masters universe_key_1 /mnt/d0/yb-data/master/keys
```

The universe key (`universe_key_1`) will be loaded in-memory to the YB-Master leader node (`ip1`) and replicated to the other YB-Master nodes (`ip2`and `ip3`).

### Step 4. Verify YB-Master nodes have the universe key

Before you can enable cluster-wide encryption, all YB-Master nodes must have the universe key loaded in-memory. 

To verify that all YB-Master nodes have the universe key, run the following `yb-admin` [`all_masters_have_universe_key_in_memory`](../../admin/yb-admin/#all-masters-have-universe-key-in-memory) command.

```sh
yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 all_masters_have_universe_key_in_memory universe_key_1
```

The results should show something like the following:

```
Node 172.151.29.141:7100 has universe key in memory: 1
Node 172.151.17.103:7100 has universe key in memory: 1
Node 172.151.24.58:7100 has universe key in memory: 1
```

### Step 5. Enable cluster-wide encryption

Run the `yb-admin` [`rotate_universe_key_in_memory`](../../admin/yb-admin/#rotate-universe-key-in-memory) command to tell the cluster about the new universe key and enable the cluster-wide encryption.

```sh
$ yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 rotate_universe_key_in_memory universe_key_1
```

{{< note title="Note" >}}

Only newly-written data is encrypted because encryption occurs in the background as part of flushes to disk and compactions. Therefore, the call should return quickly.

{{< /note >}}

### Step 6. Verify encryption enabled

To check the encryption status of the cluster, run the `yb-admin` [`is_encryption_enabled`](../../admin/yb-admin/#is-encryption-enabled) command.

```sh
$ yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 is_encryption_enabled
```

```
Encryption status: ENABLED with key id <key_id>
```

## Rotating universe keys

To improve security, you should rotate your universe keys on a regular basis.

### Step 1. Creating a new universe key

Create the key to be rotated. In this example, the `openssl rand` command is used to generate a new universe key.

```sh
$ openssl rand -out universe_key_2 [ 32 | 40 | 48 ]

```

{{< note title="Note" >}}

The new key name must be distinct from the current key name.

{{< /note >}}

### Step 2. Copy the new universe key to YB-Master nodes

As with enabling, copy the universe key to a location on the YB-Master leader node.

```sh
$ scp -i <ssh_key> -P 54422 universe_key ip1:/mnt/d0/yb-data/master/
```

### Step 3. Rotate the universe key

To tell the cluster to rotate from the existing universe key to the new key, run the `yb-admin` [`rotate_universe_key_in_memory`](../../admin/yb-admin/#rotate-universe-key-in-memory) command.

```sh
$ yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 rotate_universe_key_in_memory universe_key_2
```

### Step 4. Verify that the new key is being used

Check that the new key is encrypting the cluster by running the `yb-admin` [`is_encryption_enabled`](../../admin/yb-admin/#is-encryption-enabled) command.

```sh
$ yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 is_encryption_enabled
```

```
Encryption status: ENABLED with key id <key_id_2>
```

`<key_id_2>` should be different from the previous `<key_id>`.

## Disable encryption

### Step 1. Disable cluster-wide encryption

Run the `yb-admin` [`disable_encryption_in_memory`](../../admin/yb-admin/#disable-encryption-in-memory) command to disable cluster-wide encryption.

```sh
$ yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 disable_encryption_in_memory
```

### Step 2. Verify encryption disabled

Verify that encryption is disabled by running the `yb-admin` [`is_encryption_enabled`](../../admin/yb-admin/#is-encryption-enabled) command.

```sh
$ yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 is_encryption_enabled
```

```
Encryption status: DISABLED
```
