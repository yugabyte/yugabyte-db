---
title: Enable encryption at rest in YugabyteDB clusters
headerTitle: Encryption at rest
linkTitle: Encryption at rest
description: Enable encryption at rest in a YugabyteDB cluster with a user-generated key.
headcontent: Encrypt your YugabyteDB cluster
aliases:
  - /secure/encryption-at-rest
menu:
  stable:
    identifier: encryption-at-rest
    parent: secure
    weight: 735
type: docs
---

{{< page-finder/head text="Encryption at rest" subtle="across different products">}}
  {{< page-finder/list icon="/icons/database-hover.svg" text="YugabyteDB" current="" >}}
  {{< page-finder/list icon="/icons/server-hover.svg" text="YugabyteDB Anywhere" url="../../yugabyte-platform/security/enable-encryption-at-rest/" >}}
  {{< page-finder/list icon="/icons/cloud-hover.svg" text="YugabyteDB Aeon" url="/stable/yugabyte-cloud/cloud-secure-clusters/managed-ear/" >}}
{{< /page-finder/head >}}

You can enable and disable encryption at rest in a YugabyteDB cluster with a self-generated key.

YugabyteDB uses two layers of keys. Each data file (SST file or WAL segment) is encrypted with its own randomly generated data key, using AES in CTR mode. The data key is stored in the file header, encrypted with the cluster-wide _universe key_ that you supply. The strength of the universe key depends on the length of the key file you generate, as described in the following section.

The data key size is recorded when encryption at rest is enabled for the first time. Clusters that enable encryption at rest for the first time use 256-bit (AES-256) data keys. Clusters that enabled it earlier keep 128-bit (AES-128) data keys, including after an upgrade, until you [migrate them to AES-256](#migrate-an-existing-cluster-to-aes-256).

Note that encryption can be applied at the following levels:

- At the database layer, in which case the encryption process and its associated capabilities, such as key rotation, are cluster-wide.
- At the file system level, in which case it is the responsibility of the operations teams to manage the process manually on every node. It is important to note that the degree to which file systems or external encryption mechanisms support online operations can vary (for example, when the database processes are still running).

    If you are using third party disk encryption software, such as Vormetric or CipherTrust, the disk encryption service must be up and running on the node before starting any YugabyteDB services. If YugabyteDB processes start _before_ the encryption service, restarting an already encrypted node can result in data corruption.

    To avoid issues, stop YugabyteDB services on the node _before_ enabling or disabling the disk encryption service.

## Enable encryption

You enable encryption as follows:

1. Generate a 48-byte universe key by executing the following command on your local file system:

    ```sh
    openssl rand -out /path/to/universe_key 48
    ```

    The first 16 bytes of the key file are used as the nonce and counter, and the rest is the AES key. A 48-byte file is therefore an AES-256 key. Key files of 32 and 40 bytes are also accepted, and give AES-128 and AES-192 respectively.

1. Copy the key to master nodes. In the following example, assume a 3-node RF=3 cluster with `MASTER_ADDRESSES=ip1:7100,ip2:7100,ip3:7100`. Choose any string `<key_id>` for this key and use yb-admin to copy the key to each of the masters:

    ```sh
    yb-admin --master_addresses $MASTER_ADDRESSES add_universe_key_to_all_masters <key_id> /<path_to_universe_key>
    ```

    The preceding operation does not perform the key rotation, but rather seeds each master's in-memory state. The key only lives in memory, and the plaintext key is never persisted to the disk.

1. Enable cluster-wide encryption. Before rotating the key, ensure that the masters know about `<key_id>`:

    ```sh
    yb-admin --master_addresses $MASTER_ADDRESSES all_masters_have_universe_key_in_memory <key_id>
    ```

    If the preceding command fails, rerun step 2. Once this succeeds, instruct the cluster to start using the new universe key, as follows:

    ```sh
    yb-admin --master_addresses $MASTER_ADDRESSES rotate_universe_key_in_memory <key_id>
    ```

    Because data is encrypted in the background as part of flushes to disk and compactions, only new data is encrypted. Therefore, the call should return quickly.

1. Verify that encryption has been enabled. To do this, check the encryption status of the cluster by executing the following yb-admin command:

    ```sh
    yb-admin --master_addresses $MASTER_ADDRESSES is_encryption_enabled
    ```

    Expect the following output:

    ```output
    Encryption status: ENABLED with key id <key_id>
    ```

## Rotate new key

You can rotate the new key as follows:

1. Create the key to be rotated by executing the following command:

    ```sh
    openssl rand -out /path_to_universe_key_2 48
    ```

    Make sure to use a different key path to avoid overwriting the previous key file.

1. Copy the new key to master nodes, informing the master nodes about the new key, as follows:

    ```sh
    yb-admin --master_addresses $MASTER_ADDRESSES add_universe_key_to_all_masters
    <key_id_2> /path_to_universe_key_2
    ```

    `<key_id>` must be different from any previous keys.

1. Ensure that the masters know about the key, and then perform the rotation, as follows:

    ```sh
    yb-admin --master_addresses $MASTER_ADDRESSES rotate_universe_key_in_memory <key_id_2>
    ```

    The new universe key encrypts new data. Existing files stay under the previous universe key until compaction rewrites them. The [encryption key report](#migrate-an-existing-cluster-to-aes-256) on each YB-TServer shows how many files are still under each universe key. Keep every rotated universe key; the cluster needs it to read that data.

1. Verify the new key. To do this, check that the new key is encrypting the cluster, as follows:

    ```sh
    yb-admin --master_addresses $MASTER_ADDRESSES is_encryption_enabled
    ```

    Expect the following output:

    ```output
    Encryption status: ENABLED with key id <key_id_2>
    ```

    `<key_id_2>` must be different from the previous `<key_id>`.

## Migrate an existing cluster to AES-256

Clusters that enabled encryption at rest before AES-256 became the default, or with a 32-byte universe key, have data protected by 128-bit keys. They remain supported. You can move such a cluster to AES-256 in place, without downtime, as follows. If you are upgrading the cluster, finalize the upgrade first.

1. Rotate to a 48-byte universe key by following the steps in [Rotate new key](#rotate-new-key).

1. Switch new data files to 256-bit data keys:

    ```sh
    yb-admin --master_addresses $MASTER_ADDRESSES set_encryption_algorithm AES_256_CTR
    ```

    YB-TServers pick up the change on their next heartbeat to the YB-Master leader.

1. Perform a rolling restart of the YB-TServers, one node at a time.

    The restart is not needed for the new key size: after the previous step, new data files, including those written by compaction, already use 256-bit keys. It is needed for each tablet's RocksDB `MANIFEST` file, which records the key range of every data file. A tablet keeps appending to the same `MANIFEST`, under the key it was created with, until the tablet is reopened, so without a restart the `MANIFEST` stays on its 128-bit key after every data file has been rewritten.

1. Rewrite existing data by running a full compaction of every table, including indexes. For example:

    ```sh
    yb-admin --master_addresses $MASTER_ADDRESSES compact_table ysql.yugabyte table_name 3600
    ```

    Compaction rewrites every data file under the new keys. Because it is the first change to each tablet's file list since the restart, it also makes RocksDB write a new `MANIFEST` and `CURRENT` file under the new keys. Restarting alone does not replace them, so compact after the restart, not before. Compaction adds disk I/O and CPU load, so compact one table at a time during a low-traffic period.

1. Account for files that compaction does not rewrite:

    - Snapshots, including those taken by [snapshot schedules](../../manage/backup-restore/point-in-time-recovery/), keep the files they reference until the snapshot is deleted or expires.
    - WAL segments retained for xCluster replication, CDC, or WAL retention settings keep their keys until they are garbage-collected.
    - Each tablet's RocksDB `IDENTITY` file, which holds only a randomly generated identifier, is never rewritten.

1. Verify the result on each YB-TServer:

    ```sh
    curl "http://<tserver-host>:9000/api/v1/encryption-key-report?list_aes128_files"
    ```

    The report groups the files on the node by category (`Data`, `Wal`, `Snapshot`, `Metadata`), file type, data key size (`data_key_bits`), and the universe key that encrypts the data key (`universe_key_id`, `universe_key_bits`). `aes128_files` is the number of files still protected by a 128-bit key; with `list_aes128_files`, the report also lists their paths, up to 1000, and sets `aes128_paths_truncated` if there are more. `unreadable_files` counts files whose header could not be read, and `undecodable_files` counts encrypted files whose universe key is missing from the cluster, so their key sizes are unknown. Migration is complete when both are 0 and only the files described in the previous step remain on 128-bit keys. The report reads the header of every file on the node, so avoid running it frequently on nodes with many files.

## Disable encryption

Disabling encryption at rest stops encryption of new data. Data already on disk stays encrypted until compaction rewrites it, and the cluster still needs the universe keys to read that data.

You can disable cluster-wide encryption as follows:

1. Disable encryption by executing the following yb-admin command:

    ```sh
    yb-admin --master_addresses $MASTER_ADDRESSES disable_encryption
    ```

1. Verify that the encryption has been disabled by executing the following command:

    ```sh
    yb-admin --master_addresses $MASTER_ADDRESSES is_encryption_enabled
    ```

    Expect the following output:

    ```output
    Encryption status: DISABLED
    ```


{{< note title="Keep your keys" >}}

Store your keys securely, including those that have been rotated. The cluster needs those keys to read data that has not yet been rewritten under the latest universe key. If you want to restore distributed snapshots of an encrypted cluster to a new cluster, you need to copy the keys used for encryption (including rotated keys) to the new cluster. (Backup, restore, and key management are automated in YugabyteDB Anywhere.)
{{< /note >}}
