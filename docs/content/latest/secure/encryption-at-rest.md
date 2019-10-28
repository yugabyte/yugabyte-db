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

Encryption at rest ensures that data stored on disk is protected. You can configure your YugabyteDB cluster with a user-generated symmetric key to perform cluster-wide encryption. Follow the steps below to enable or disable encryption at rest in a YugabyteDB cluster.

{{< note title="Note" >}}

Encryption at rest is currently not supported with YSQL.

{{< /note >}}

## Enabling encryption

### Step 1. Create encryption key

First, we will generate the universe key data. This data can have length 32, 40, or 48. Larger keys
are slightly more secure with slightly worse performance. Run the following on your local
filesystem.

```sh
$ openssl rand -out universe_key [ 32 | 40 | 48 ]

```

### Step 2. Copy key to master nodes

In this example, we assume a 3 node RF=3 cluster with addresses ip1, ip2, ip3.
Copy the universe key onto each master filesystem, in the same location on every node.

```sh
$ for ip in ip1 ip2 ip3
  do
    scp -i <ssh_key> -P 54422 universe_key ip:/mnt/d0/yb-data/master
  done
```

{{< note title="Note" >}}
The key can be added to any subdirectory of the master directory, as long as it lives in the same place on each
node. In addition, the location of the data directory may vary depending on how the cluster is created.
{{< /note >}}

### Step 3. Enable cluster-wide encryption

Use `yb-admin` command [`rotate_universe_key`](../../admin/yb-admin/#rotate-universe-key) to tell the cluster about the new universe key.

```sh
$ yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 rotate_universe_key
/mnt/d0/yb-data/master/universe_key
```

{{< note title="Note" >}}

Only new data is encrypted because encryption occurs in the background as part of flushes to disk and compactions. Therefore, the call should return quickly.

{{< /note >}}

### Step 4. Verify encryption enabled

To check the encryption status of the cluster, run the `yb-admin` [`is_encryption_enabled`](../../admin/yb-admin/#is-encryption-enabled) command.

```sh
$ yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 is_encryption_enabled
```

```
Encryption status: ENABLED with key id <key_id>
```

## Rotating a new key

### Step 1. Creating a new key

First we create the key to be rotated.

```sh
$ openssl rand -out universe_key_2 [ 32 | 40 | 48 ]

```

{{< note title="Note" >}}

The new key name must be distinct from the previous key name.

{{< /note >}}

### Step 2. Copy new key to master nodes

As with enabling, copy the universe key onto each master filesystem,
in the same location on every node.

```sh
$ for ip in ip1 ip2 ip3
  do
    scp -i <ssh_key> -P 54422 universe_key ip:/mnt/d0/yb-data/master/
  done
```

### Step 3. Rotate key

To tell the cluster about the new universe key, run the `yb-admin` [`rotate_universe_key`](../../admin/yb-admin/#rotate-universe-key) command.

```sh
$ yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 rotate_universe_key
/mnt/d0/yb-data/master/universe_key_2
```

### Step 4. Verify new key

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

Use yb-admin to disable encryption.

```sh
$ yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 disable_encryption
```

### Step 2. Verify encryption disabled

Verify that encryption is disabled by running the `yb-admin` [`is_encryption_enabled`](../../admin/yb-admin/#is-encryption-enabled) command.

```sh
$ yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 is_encryption_enabled
```

```
Encryption status: DISABLED
```
