---
title: Enable encryption at rest in YugabyteDB clusters
headerTitle: Encryption at rest
linkTitle: Encryption at rest
description: Enable encryption at rest in a YugabyteDB cluster with a user-generated key.
headcontent: Enable encryption at rest with a user-generated key
image: /images/section_icons/secure/prepare-nodes.png
menu:
  v2.14:
    identifier: encryption-at-rest
    parent: secure
    weight: 735
type: docs
---

This page describes how to enable and disable encryption at rest in a YugabyteDB cluster with a user-generated key.

## Enabling encryption

### Step 1. Create encryption key

First, you will generate the universe key data. This data can have length 32, 40, or 48. Larger keys are more secure with slightly worse performance. Run the following on your local filesystem.

```sh
$ openssl rand -out /path/to/universe_key [ 32 | 40 | 48 ]

```

### Step 2. Copy key to master nodes

In this example, assume a 3 node RF=3 cluster with `MASTER_ADDRESSES=ip1:7100,ip2:7100,ip3:7100`. Choose any string <key_id> for this key and use yb-admin to copy the key to each of the masters.

```sh
$ yb-admin -master_addresses $MASTER_ADDRESSES add_universe_key_to_all_masters \
           <key_id> /path/to/universe_key
```

{{< note title="Note" >}}
This operation doesn't actually perform the key rotation, but rather seeds each master's in-memory state. The key only lives in-memory, and the plaintext key will never be persisted to disk.
{{< /note >}}

### Step 3. Enable cluster-wide encryption

Before rotating the key, make sure the masters know about <key_id>.

```sh
yb-admin -master_addresses $MASTER_ADDRESSES all_masters_have_universe_key_in_memory <key_id>
```

If this fails, re-run step 2. Once this succeeds, tell the cluster to start using new universe key.

```sh
$ yb-admin -master_addresses $MASTER_ADDRESSES rotate_universe_key_in_memory <key_id>
```

{{< note title="Note" >}}
Because data is encrypted in the background as part of flushes to disk and compactions, only new data will be encrypted. Therefore, the call should return quickly.
{{< /note >}}

### Step 4. Verify encryption enabled

To check the encryption status of the cluster, run the following yb-admin command.

```sh
$ yb-admin -master_addresses $MASTER_ADDRESSES is_encryption_enabled
```

```output
Encryption status: ENABLED with key id <key_id>
```

## Rotating a new key

### Step 1. Creating a new key

First, create the key to be rotated.

```sh
$ openssl rand -out /path/to/universe_key_2 [ 32 | 40 | 48 ]
```

{{< note title="Note" >}}
Make sure to use a different key path to avoid overwriting the previous key file.
{{< /note >}}

### Step 2. Copy new key to master nodes

As with enabling, tell the master nodes about the new key.

```sh
$ yb-admin -master_addresses $MASTER_ADDRESSES add_universe_key_to_all_masters
<key_id_2> /path/to/universe_key_2
```

{{< note title="Note" >}}
Make sure the <key_id> is different from any previous keys.
{{< /note >}}

### Step 3. Rotate key

Do the same validation as enabling that the masters know about the key and then perform the rotation.

```sh
$ yb-admin -master_addresses $MASTER_ADDRESSES rotate_universe_key_in_memory <key_id_2>
```

{{< note title="Note" >}}
Since this key will only be used for new data and will only eventually encrypt older data through compactions, it is best to ensure old keys remain secure.
{{< /note >}}

### Step 4. Verify new key

Check that the new key is encrypting the cluster.

```sh
$ yb-admin -master_addresses $MASTER_ADDRESSES is_encryption_enabled
```

```output
Encryption status: ENABLED with key id <key_id_2>
```

`<key_id_2>` should be different from the previous `<key_id>`.

## Disable encryption

### Step 1. Disable cluster-wide encryption

Use yb-admin to disable encryption.

```sh
$ yb-admin -master_addresses $MASTER_ADDRESSES disable_encryption
```

### Step 2. Verify encryption disabled

Check that encryption is disabled.

```sh
$ yb-admin -master_addresses $MASTER_ADDRESSES is_encryption_enabled
```

```output
Encryption status: DISABLED
```
