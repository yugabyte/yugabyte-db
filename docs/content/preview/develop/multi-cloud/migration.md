---
title: Multicloud Migration
headerTitle: Multicloud Migration
linkTitle: Multicloud Migration
description: Migrate your data between different clouds
headcontent: Migrate your data between different clouds
menu:
  preview:
    identifier: multicloud-migration
    parent: build-multicloud-apps
    weight: 300
type: docs
---

Moving your application either from an on-prem data center to a public cloud or from one public cloud to another is a non trivial task. YugabyteDB provides a simple pattern for you to accomplish this daunting task with elegance and ease. Let us go over the procedure.

## Migration between two clouds

For illustration let us consider a scenario where you have a 3-node cluster deployed in AWS-`us-west`. and would like to move to GCP-`us-central`.

![Multicloud Migration](/images/develop/multicloud/multicloud-migration-goal.png)

To accomplish this, we will use the [xCluster](../../../architecture/docdb-replication/async-replication/) feature which provides one-way asynchronous replication from one universe to another for which the `AWS` will be the source and the `GCP` universe will be the target.

## Set up the universes

Set up the **Source** universe in `AWS` and the **Target** universe in `GCP`. using the following setup procedures.

{{<warning title="TODO-Setup">}}
Will add tabs for Local/YBAnywhere/YBManaged
{{</warning>}}

Now, note down the universe-uuids of the `source` and `target` universes.

## Bootstrapping

Now that the `GCP` universe has been set up, you need to populate the data from your `AWS` universe. It involves a few steps.

1. Creating a checkpoint for all the tables in the `AWS` universe. For example,

```bash
./bin/yb-admin -master_addresses <source_universe_master_addresses> \
        bootstrap_cdc_producer <comma_separated_source_universe_table_ids>
```

1. Taking the backup of the tables on the `AWS` universe and restoring at the `GCP` universe. c.f [Backup and Restore](../../../manage/backup-restore/)

This will ensure that the current data in your `AWS` universe is correctly copied over to the `GCP` universe. This procedure is known as Bootstrapping.

{{<tip title="More Details">}}
For detailed instructions, see [Bootstrap a target universe](../../../deploy/multi-dc/async-replication/#bootstrap-a-target-universe)
{{</tip>}}

## Set up replication

Now that your data has been pre-populated from the `AWS` universe to the `GCP` universe, you need to set up the replication stream from `AWS` to the `GCP` universe. For example,

```bash
./bin/yb-admin -master_addresses <target_universe_master_addresses> setup_universe_replication \
  <source_universe_uuid>_<replication_stream_name> <source_universe_master_addresses> \
  <comma_separated_source_universe_table_ids> <comma_separated_bootstrap_ids>
```

![Multicloud Replication](/images/develop/multicloud/multicloud-migration-replication.png)

{{<tip title="More Details">}}
For detailed instructions, on how to set up replication, see [Set up unidirectional replication](../../../deploy/multi-dc/async-replication/#set-up-unidirectional-replication)
{{</tip>}}

Now any data changes to the `AWS` universe are automatically applied to the `GCP` universe. _NOTE_: For now, DDL changes have to be applied manually.

## Cutover

Before you cutover from the `AWS` to the `GCP` universe verify if the data has been successfully migrated using simple techniques like validating the no.of rows of the corresponding tables in both universes are same(eg `SELECT count(*)`).

Now you can point your apps to the new universe in `GCP` and then stop the replication. For eg,

```bash
yb-admin \
    -master_addresses <target_master_ips> \
    -certs_dir_name <cert_dir> \
    set_universe_replication_enabled <replication_group_name> 0
```

![Migration complete](/images/develop/multicloud/multicloud-migration-complete.png)

At this point, you can either tear-down your `AWS` universe or convert it to stand-by cluster for Disaster Recovery, by setting up an [xCluster](../../../architecture/docdb-replication/async-replication/) replication from the new `GCP` universe to the old `AWS` universe.