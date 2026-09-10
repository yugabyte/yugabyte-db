---
title: High Availability promotion
headerTitle: Promote a standby
description: Failover or switchover to a standby HA instance
headcontent: Failover or switchover to a standby HA instance
linkTitle: Promote standby
menu:
  stable_yugabyte-platform:
    identifier: platform-high-availability-promote
    parent: platform-high-availability
    weight: 20
type: docs
---

Both switchover and failover to promote a High Availability (HA) standby must be done manually.

In the case of a failure of your active HA instance, where the previous active instance is unavailable or unreachable during promotion, _failover is not automatic_. In the case of failover, you promote the standby using the **Force promotion** option.

## Promotion and old backups

Immediately after [upgrading the active instance](../high-availability/#upgrade-instances) to a new version of YBA, older state backups of the active instance (that is, before it was upgraded) will still be available on the standby. These are not deleted until the standby is promoted at some point, or until they expire.

Because these old backups are present, you need to be cautious promoting the standby in the time immediately following an upgrade.

When possible, only promote a standby when both standby and active are on the same version, and use the most recent backup that you are confident was received after the active instance was upgraded.

## Promote a standby instance to active

You can make a standby instance active as follows:

1. On the standby instance you want to promote, navigate to **Admin > High Availability > Replication Configuration** and click **Make Active**.

1. Select the backup from which you want to restore (in most cases, you should choose the most recent backup) and enable **Confirm promotion**.

    {{< warning title="Do not promote an old backup" >}}

If you recently upgraded the active instance, ensure the backup you select is up to date. See [Promotion and old backups](#promotion-and-old-backups).

    {{< /warning >}}

1. If you are performing a failover, where the previous active instance is unavailable or unreachable during promotion, select the **Force promotion** option.

    This will promote the standby without demoting the active.

1. Click **Continue**. The restore takes a few seconds, after which expect to be signed out.

1. Sign in using the credentials that you had configured on the previously active instance. If you are performing failover, you must sign in using your Super Admin account.

    You should be able to see that all of the data has been restored into the instance, including universes, users, metrics, alerts, task history, provider configurations, and so on.

1. In the case of failover, follow the steps in [Failover](#failover) to ensure that the old active does not come back up or that it goes into standby mode when it does come up.

## Verify promotion

After switching or failing over to the standby, verify that the old active YBA instance is in standby mode (switchover), or is no longer available (failover).

If both YBA instances were to attempt to perform actions on a universe, it could have unpredictable side effects. It is critical to ensure that the old active instance is taken out of service or re-imaged as soon as possible if it is unavailable.

YugabyteDB release archives are not synchronized between the active and standby instances. If any custom releases were added to the old active instance, you will need to add them to the new active instance again. The _Universe Release Files Missing_ alert will fire on any universes that are missing their corresponding release archives. If this alert fires, follow the steps in [How to Configure YugabyteDB Anywhere to provide Older, Hotfix, or Debug Builds](https://support.yugabyte.com/hc/en-us/articles/360054421952-How-to-configure-YugabyteDB-Anywhere-to-provide-Older-Hotfix-or-Debug-Builds).

### Switchover

After a switchover, do the following:

- [Verify that HA is functioning properly](../high-availability/#verify-ha).
- If the old active instance is not in standby mode, there could be a communication issue from the new active to the old active instance. Follow the [setup instructions](../high-availability/#set-up-high-availability) to verify that certificates and ports are set up correctly.

### Failover

After a failover, do the following:

- If the old active instance is hard down, verify that there is no chance that it can come back and run YBA at a later point. It is recommended to re-image the server hosting the active instance.
- If the old active instance does come back up, it should automatically go into standby mode. If it does not go into standby mode, you should manually demote it using the YBA API. Refer to [High Availability Workflows](https://github.com/yugabyte/yugabyte-db/blob/master/managed/api-examples/python-simple/high-availability.ipynb) for an example.

- If the old active instance has successfully switched to standby, [verify that HA is functioning properly](../high-availability/#verify-ha).
