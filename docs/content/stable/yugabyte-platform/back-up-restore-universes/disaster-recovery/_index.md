---
title: Configure disaster recovery for a YugabyteDB Anywhere universe
headerTitle: Disaster recovery
linkTitle: Disaster recovery
description: Enable deployment using transactional (active-standby) replication between universes
headContent: Configure disaster recovery
menu:
  stable_yugabyte-platform:
    parent: back-up-restore-universes
    identifier: disaster-recovery
    weight: 90
type: indexpage
showRightNav: true
---

Use disaster recovery (DR) to recover from an unplanned outage (failover) or for planned switchover.

A DR configuration consists of a primary universe, which serves both reads and writes, and a DR replica universe. Data from the primary is replicated asynchronously to the DR replica (which is read only). Due to the asynchronous nature of the replication, this deployment comes with non-zero recovery point objective (RPO) in the case of a primary universe outage. The actual value depends on the replication lag, which in turn depends on the network characteristics between the universes.

The recovery time objective (RTO) is very low, as it only depends on the applications switching their connections from one universe to another. Applications should be designed in such a way that the switch happens as quickly as possible.

DR further allows for the role of each universe to switch during planned switchover and unplanned failover scenarios.

![Disaster recovery](/images/deploy/xcluster/xcluster-transactional.png)

## Prerequisites

Create two universes, the primary universe which will serve reads and writes, and the DR replica.

Ensure the universes have the following characteristics:

- Both universes have the same encryption in transit settings.
- They reside in different regions.
- They use the same backup configuration.
- They have enough disk space. DR requires more disk space to store write ahead logs (WAL) in case of a network partition or a complete outage of the DR replica universe.

Prepare your database and tables on the DR primary. The DR primary can be empty or have data. If the DR primary has a lot of data, the DR setup will take longer as the data must be copied to the DR replica before replication starts.

On the DR replica, create a database with the same name as that on the DR primary. During initial DR setup, you don't need to create objects on the DR replica. DR performs a full copy of the data to be replicated on the DR primary and automatically creates tables and objects and restores data on the DR replica from the DR primary.

After DR is configured, the DR replica will only be available for reads.

## Limitations

- If there are any connections open against the DR replica databases and DR is being set up for a DR primary database that already has some data, then DR setup will fail with an expected error. This is because setting up DR requires backing up the primary database and restoring the backup to the target database after cleaning up any pre-existing data on the target side. Close any connections to the DR replica database and retry the setup operation.

    ```output
    Error ERROR:  database "database_name" is being accessed by other users
    DETAIL:  There is 1 other session using the database.
    ```


## Best practices

- Keep CPU use below 65%.
- Keep disk space use under 65%.
- Create the DR primary and DR replica universes with TLS enabled.
- Set the YB-TServer [log_min_seconds_to_retain](../../../../reference/configuration/yb-tserver/#log-min-seconds-to-retain) to 86400 on both DR primary and replica.

    This flag determines the duration for which WAL is retained on the primary universe in case of a network partition or a complete outage of the DR replica universe. Be sure to allocate enough disk space to hold WAL generated for this duration.

    The value depends on how long a network partition or DR replica cluster outage can be tolerated, and the amount of WAL expected to be generated during that period.
