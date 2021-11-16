---
title: Manual remote bootstrap when a majority of peers fail
linkTitle: Manual remote bootstrap when a majority of peers fail
description: Manual remote bootstrap when a majority of peers fail
menu:
  v2.6:
    parent: troubleshoot-cluster
    weight: 835
isTocNested: true
showAsideToc: true
---

When a RAFT peer fails, YugabyteDB executes an automatic remote bootstrap to create a new peer from the remaining ones.
If a majority of RAFT peers fail for a given tablet, we have to manually execute the equivalent of a remote bootstrap. We
 can get a list of tablets in `yb-master-ip:7000/tablet-replication` yb-admin gui. 


Assuming we have a cluster where:

- Replication factor is 3
- a given tablet with UUID `TABLET1`
- 3 tablet peers, 1 in good working order, referred to as `NODE_GOOD` and two broken peers, referred as `NODE_BAD1` and `NODE_BAD2`
- We will be copying some tablet related data from the good peer to each of the bad peers until we've restored the majority of them

These are the steps to follow in such scenario:

- on the `NODE_GOOD` TS, create an archive of the wals (raft data), rocksdb (regular rocksdb) directories, intents (transactions data) and snapshots directories for `TABLET1`

- copy these archives over to `NODE_BAD1`, on the same drive that `TABLET1` currently has its raft and rocksdb data

- stop the bad TS, say `NODE_BAD1`, as we will be changing file system data underneath

- remove the old wals, rocksdb, intents, snapshots data for `TABLET1` from `NODE_BAD1`

- unpack the data we copied over from `NODE_GOOD` into the corresponding (now empty) directories on `NODE_BAD1`

- restart `NODE_BAD1`, so it can bootstrap `TABLET1` using this new data

- restart `NODE_GOOD` so it can properly observe the changed state and data on `NODE_BAD1`

At this point, `NODE_BAD2` should be automatically fixed and removed from its quorum, as it has gotten a majority of healthy peers.

{{< note title="Note" >}}

Normally when we try to find tablet data, we use a `find` command across the `--fs_data_dir` paths. 

In this example, assume that's set to `/mnt/d0` and our tablet UUID is `c08596d5820a4683a96893e092088c39`:

```bash
$ find /mnt/d0/ -name '*c08596d5820a4683a96893e092088c39*'
/mnt/d0/yb-data/tserver/wals/table-2fa481734909462385e005ba23664537/tablet-c08596d5820a4683a96893e092088c39
/mnt/d0/yb-data/tserver/tablet-meta/c08596d5820a4683a96893e092088c39
/mnt/d0/yb-data/tserver/consensus-meta/c08596d5820a4683a96893e092088c39
/mnt/d0/yb-data/tserver/data/rocksdb/table-2fa481734909462385e005ba23664537/tablet-c08596d5820a4683a96893e092088c39
/mnt/d0/yb-data/tserver/data/rocksdb/table-2fa481734909462385e005ba23664537/tablet-c08596d5820a4683a96893e092088c39.intents
/mnt/d0/yb-data/tserver/data/rocksdb/table-2fa481734909462385e005ba23664537/tablet-c08596d5820a4683a96893e092088c39.snapshots
```

The data we are interested in here is:

For the raft wals: 
```bash
/mnt/d0/yb-data/tserver/wals/table-2fa481734909462385e005ba23664537/tablet-c08596d5820a4683a96893e092088c39
```

For the rocksdb regular DB: 
```bash
/mnt/d0/yb-data/tserver/data/rocksdb/table-2fa481734909462385e005ba23664537/tablet-c08596d5820a4683a96893e092088c39
```

For the intents files: 
```bash
/mnt/d0/yb-data/tserver/data/rocksdb/table-2fa481734909462385e005ba23664537/tablet-c08596d5820a4683a96893e092088c39.intents
```

For the snapshot files: 
```bash
/mnt/d0/yb-data/tserver/data/rocksdb/table-2fa481734909462385e005ba23664537/tablet-c08596d5820a4683a96893e092088c39.snapshots
```

{{< /note >}}

