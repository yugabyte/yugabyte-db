---
title: Enable encryption at rest in YugabyteDB clusters
headerTitle: Encryption at rest
linkTitle: Encryption at rest
description: Enable encryption at rest in a YugabyteDB cluster with a user-generated key.
headcontent: 
aliases:
  - /secure/encryption-at-rest
menu:
  preview:
    identifier: encryption-at-rest
    parent: secure
    weight: 735
type: docs
---

You can enable and disable encryption at rest in a YugabyteDB cluster with a self-generated key.

Note that encryption can be applied at the following levels:

- At the database layer, in which case the encryption process and its associated capabilities, such as key rotation, are cluster-wide.
- At the file system level, in which case it is the responsibility of the operations teams to manage the process manually on every node. It is important to note that the degree to which file systems or external encryption mechanisms support online operations can vary (for example, when the database processes are still running).

## Enable encryption

You enable encryption as follows:

1. Generate the universe key data of length 32, 40, or 48 by executing the following command on your local file system:

    ```sh
    openssl rand -out /path/to/universe_key [ 32 | 40 | 48 ]
    ```

    Note that larger keys are more secure with slightly worse performance.

1. Copy the key to master nodes. In the following example, assume a 3-node RF=3 cluster with `MASTER_ADDRESSES=ip1:7100,ip2:7100,ip3:7100`. Choose any string `<key_id>` for this key and use yb-admin to copy the key to each of the masters:

    ```sh
    yb-admin -master_addresses $MASTER_ADDRESSES add_universe_key_to_all_masters <key_id> /<path_to_universe_key>
    ```

    The preceding operation does not perform the key rotation, but rather seeds each master's in-memory state. The key only lives in memory, and the plaintext key is never persisted to the disk.

1. Enable cluster-wide encryption. Before rotating the key, ensure that the masters know about `<key_id>`:

    ```sh
    yb-admin -master_addresses $MASTER_ADDRESSES all_masters_have_universe_key_in_memory <key_id>
    ```

    If the preceding command fails, rerun step 2. Once this succeeds, instruct the cluster to start using the new universe key, as follows:

    ```sh
    yb-admin -master_addresses $MASTER_ADDRESSES rotate_universe_key_in_memory <key_id>
    ```

    Because data is encrypted in the background as part of flushes to disk and compactions, only new data is encrypted. Therefore, the call should return quickly.

1. Verify that encryption has been enabled. To do this, check the encryption status of the cluster by executing the following yb-admin command:

    ```sh
    yb-admin -master_addresses $MASTER_ADDRESSES is_encryption_enabled
    ```

    Expect the following output:

    ```output
    Encryption status: ENABLED with key id <key_id>
    ```

## Rotate new key

You can rotate the new key as follows:

1. Create the key to be rotated by executing the following command:

    ```sh
    openssl rand -out /path_to_universe_key_2 [ 32 | 40 | 48 ]
    ```

    Make sure to use a different key path to avoid overwriting the previous key file.

1. Copy the new key to master nodes, informing the master nodes about the new key, as follows:

    ```sh
    yb-admin -master_addresses $MASTER_ADDRESSES add_universe_key_to_all_masters
    <key_id_2> /path_to_universe_key_2
    ```

    `<key_id>` must be different from any previous keys.

1. Ensure that the masters know about the key, and then perform the rotation, as follows:

    ```sh
    yb-admin -master_addresses $MASTER_ADDRESSES rotate_universe_key_in_memory <key_id_2>
    ```

    Because this key is only used for new data and can only eventually encrypt older data through compactions, it is best to ensure old keys remain secure.

1. Verify the new key. To do this, check that the new key is encrypting the cluster, as follows:

    ```sh
    yb-admin -master_addresses $MASTER_ADDRESSES is_encryption_enabled
    ```

    Expect the following output:

    ```output
    Encryption status: ENABLED with key id <key_id_2>
    ```

    `<key_id_2>` must be different from the previous `<key_id>`.

## Disable encryption

You can disable cluster-wide encryption as follows:

1. Disable encryption by executing the following yb-admin command:

    ```sh
    yb-admin -master_addresses $MASTER_ADDRESSES disable_encryption
    ```

1. Verify that the encryption has been disabled by executing the following command:

    ```sh
    yb-admin -master_addresses $MASTER_ADDRESSES is_encryption_enabled
    ```

    Expect the following output:

    ```output
    Encryption status: DISABLED
    ```


{{< note title="Note" >}}

When taking snapshots, you will have to separately track the decryption key(s) that were used on the cluster. 
When restoring the snapshot, you will need to add these keys to the cluster so the data can be decrypted. This is automated in YugabyteDB Anywhere.

{{< /note >}}
