---
title: Manual remote bootstrap of failed peer
linkTitle: Manual remote bootstrap of failed peer
description: Manual remote bootstrap when a majority of peers fail
menu:
  preview:
    parent: troubleshoot-cluster
    weight: 835
type: docs
---

When a Raft peer fails, YugabyteDB executes an automatic remote bootstrap to create a new peer from the remaining ones.

If a majority of Raft peers fail for a given tablet, you need to manually execute a remote bootstrap. A list of tablets is available via the `yb-master-ip:7000/tablet-replication` yb-admin UI.

Assume you have a cluster where the following applies:

- Replication factor is 3.
- A tablet with UUID `TABLET1`.
- Three tablet peers, with one in good working order, referred to as `NODE_GOOD`, and two broken peers referred to as `NODE_BAD1` and `NODE_BAD2`.
- Some of the tablet-related data is to be copied from the good peer to each of the bad peers until the majority of them are restored.

These are the steps to follow:

- Delete the tablet from the broken peers if necessary, by running:

    ```sh
    yb-ts-cli --server_address=NODE_BAD1 delete_tablet TABLET1
    yb-ts-cli --server_address=NODE_BAD2 delete_tablet TABLET1
    ```

- Trigger a remote bootstrap of `TABLET1` from `NODE_GOOD` to `NODE_BAD1`.

    ```sh
    yb-ts-cli --server_address=NODE_BAD1 remote_bootstrap NODE_GOOD TABLET1
    ```

After the remote bootstrap finishes, `NODE_BAD2` should be automatically removed from the quorum and `TABLET1` fixed, as it has gotten a majority of healthy peers.

If you can't perform the preceding steps, you can do the following to manually execute the equivalent of a remote bootstrap:

- On `NODE_GOOD`, create an archive of the WALS (Raft data), RocksDB (regular) directories, intents (transactions data), and snapshots directories for `TABLET1`.

- Copy these archives over to `NODE_BAD1`, on the same drive that `TABLET1` currently has its Raft and RocksDB data.

- Stop `NODE_BAD1`, as the file system data underneath will change.

- Remove the old WALS, RocksDB, intents, snapshots data for `TABLET1` from `NODE_BAD1`.

- Unpack the data copied from `NODE_GOOD` into the corresponding (now empty) directories on `NODE_BAD1`.

- Restart `NODE_BAD1` so it can bootstrap `TABLET1` using this new data.

- Restart `NODE_GOOD` so it can properly observe the changed state and data on `NODE_BAD1`.

At this point, `NODE_BAD2` should be automatically removed from the quorum and `TABLET1` fixed, as it has gotten a majority of healthy peers.

Note that typically, when you try to find tablet data, you would use a `find` command across the `--fs_data_dir` paths.

In the following example, assume that is set to `/mnt/d0` and your tablet UUID is `c08596d5820a4683a96893e092088c39`:

```bash
find /mnt/d0/ -name '*c08596d5820a4683a96893e092088c39*'
/mnt/d0/yb-data/tserver/wals/table-2fa481734909462385e005ba23664537/tablet-c08596d5820a4683a96893e092088c39
/mnt/d0/yb-data/tserver/tablet-meta/c08596d5820a4683a96893e092088c39
/mnt/d0/yb-data/tserver/consensus-meta/c08596d5820a4683a96893e092088c39
/mnt/d0/yb-data/tserver/data/rocksdb/table-2fa481734909462385e005ba23664537/tablet-c08596d5820a4683a96893e092088c39
/mnt/d0/yb-data/tserver/data/rocksdb/table-2fa481734909462385e005ba23664537/tablet-c08596d5820a4683a96893e092088c39.intents
/mnt/d0/yb-data/tserver/data/rocksdb/table-2fa481734909462385e005ba23664537/tablet-c08596d5820a4683a96893e092088c39.snapshots
```

The data you would be interested is the following:

- For the Raft WALS:

  ```bash
  /mnt/d0/yb-data/tserver/wals/table-2fa481734909462385e005ba23664537/tablet-c08596d5820a4683a96893e092088c39
  ```

- For the RocksDB regular database:

  ```bash
  /mnt/d0/yb-data/tserver/data/rocksdb/table-2fa481734909462385e005ba23664537/tablet-c08596d5820a4683a96893e092088c39
  ```

- For the intents files:

  ```bash
  /mnt/d0/yb-data/tserver/data/rocksdb/table-2fa481734909462385e005ba23664537/tablet-c08596d5820a4683a96893e092088c39.intents
  ```

- For the snapshot files:

  ```bash
  /mnt/d0/yb-data/tserver/data/rocksdb/table-2fa481734909462385e005ba23664537/tablet-c08596d5820a4683a96893e092088c39.snapshots
  ```
