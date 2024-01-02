---
title: Multi-cloud Migration
headerTitle: Multi-cloud migration
linkTitle: Multi-cloud migration
description: Migrate your data between different clouds
headcontent: Migrate your data between different clouds
menu:
  preview:
    identifier: multicloud-migration
    parent: build-multicloud-apps
    weight: 200
type: docs
---

Moving applications from an on-premises data center to a public cloud, or from one public cloud to another, is a non-trivial task. YugabyteDB provides a simple pattern for you to accomplish this task with comparative ease.

## Migration between two clouds

To illustrate, consider a scenario where you have a 3-node cluster deployed in AWS (us-west) and would like to move to GCP (us-central).

![Multi-cloud Migration](/images/develop/multicloud/multicloud-migration-goal.png)

To accomplish the migration, use the [xCluster](../../../architecture/docdb-replication/async-replication/) feature, which provides one-way asynchronous replication from one universe to another. In this case, AWS will be the source and the GCP universe will be the target.

### Set up the universes

Set up the **Source** universe in AWS and the **Target** universe in GCP using the following setup procedures.

<!-- begin: nav tabs -->
{{<nav/tabs list="local,anywhere" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
{{<note>}}
To set up a local universe, refer to <a href="../../../explore/#set-up-yugabytedb-universe">Set up a YugabyteDB universe</a>.
{{</note>}}

<!-- END: local cluster setup instructions -->
{{</nav/panel>}}
<!-- xCluster not currently supported in YBM
{{<nav/panel name="cloud">}} {{<setup/cloud>}} {{</nav/panel>}}
-->
{{<nav/panel name="anywhere">}}

{{<note>}}
To set up a universe in YugabyteDB Anywhere, refer to [Create a multi-zone universe](../../../yugabyte-platform/create-deployments/create-universe-multi-zone/).
{{</note>}}

<!-- END: YBA cluster setup instructions -->
{{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

When finished, note down the universe-uuids of the `source` and `target` universes.

### Bootstrap the new universe

After the GCP universe has been set up, you need to populate the data from your AWS universe. This is typically referred to as **Bootstrapping**.

{{<tip title="More Details">}}
For detailed instructions, see [Bootstrap a target universe](../../../deploy/multi-dc/async-replication/#bootstrap-a-target-universe).
{{</tip>}}

The basic flow of bootstrapping is as follows:

1. Create a checkpoint for all the tables in the AWS universe. For example:

    ```bash
    ./bin/yb-admin -master_addresses <AWS_universe_master_addresses> \
            bootstrap_cdc_producer <comma_separated_source_universe_table_ids>
    ```

1. Take a backup of the tables on the AWS universe and restore them to the GCP universe. See [Backup and Restore](../../../manage/backup-restore/).

This ensures that the current data in your AWS universe is correctly copied over to the GCP universe.

### Set up replication

After your data has been pre-populated from the AWS universe to the GCP universe, you need to set up the replication stream from the AWS to the GCP universe.

{{<tip title="More Details">}}
For detailed instructions on how to set up replication, see [Set up unidirectional replication](../../../deploy/multi-dc/async-replication/#set-up-unidirectional-replication).
{{</tip>}}

A simple way to set up replication is as follows:

```bash
./bin/yb-admin -master_addresses <GCP_universe_master_addresses> setup_universe_replication \
  <AWS_universe_uuid>_<replication_stream_name> <AWS_universe_master_addresses> \
  <comma_separated_source_universe_table_ids> <comma_separated_bootstrap_ids>
```

![Multi-cloud Replication](/images/develop/multicloud/multicloud-migration-replication.png)

Any data changes to the AWS universe are automatically applied to the GCP universe. _NOTE_: For this example, DDL changes have to be applied manually.

### Switch over to the new universe

After the new universe has caught up with the data from the old universe, you can switch over to the new universe.

{{<tip title="More Details">}}
For detailed instructions on how to do planned switchover, see [Planned switchover](../../../deploy/multi-dc/async-replication-transactional/#switchover-planned-failover).
{{</tip>}}

The basic flow of switchover is as follows:

- Verify that the data has been successfully migrated using basic techniques like validating the number of rows of the corresponding tables in both universes are the same (for example, `SELECT count(*)`).
- Stop the application traffic to the old universe to ensure no more data changes are attempted.
- Pause the replication on the new universe.
- Promote the GCP universe to `ACTIVE`. For example:

  ```bash
  ./bin/yb-admin \
      -master_addresses <GCP_master_addresses> \
      -certs_dir_name <cert_dir> \
      change_xcluster_role ACTIVE
  ```

You can point your applications to the new universe in GCP and then stop the replication, as shown in the following illustration.

![Migration complete](/images/develop/multicloud/multicloud-migration-complete.png)

At this point, you can either tear down your AWS universe or convert it to a stand-by cluster for disaster recovery, by setting up an [xCluster](../../../architecture/docdb-replication/async-replication/) replication from the new GCP universe to the AWS universe.

## Migration from on-premises data centers

You can use the same procedure to migrate data from your on-premises data centers to the public cloud. Moving from on-premises data centers to a public cloud requires a more cautious approach. You might want to consider a Hybrid cloud before moving completely to a public cloud. Refer to [Hybrid Cloud](../hybrid-cloud) for more details.

## Learn more

- [Active-active single-master application design pattern](../../../develop/build-global-apps/active-active-single-master/)
- [Hybrid Cloud](../hybrid-cloud)
