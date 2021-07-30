Tracking GitHub Issue: [8963](https://github.com/yugabyte/yugabyte-db/issues/8963)

# xCluster replication management through Platform

[Yugabyte Platform](https://docs.yugabyte.com/latest/yugabyte-platform/) includes a powerful graphical user interface for managing fleets of database clusters deployed across zones, regions, and clouds from one place. Yugabyte's users rely on the Platform console to deliver YugabyteDB as a private DBaaS through streamlined operations and consolidated monitoring.

xCluster replication enables asynchronous replication between independent YugabyteDB clusters. xCluster replication addresses use cases that do not require synchronous replication or justify the additional complexity and operating costs associated with managing three or more data centers. For these needs, YugabyteDB supports two data center (2DC) deployments that use asynchronous replication built on top of [change data capture (CDC)](https://docs.yugabyte.com/latest/architecture/docdb-replication/change-data-capture) in DocDB. For more details refer [xCluster replication](https://docs.yugabyte.com/latest/architecture/docdb-replication/async-replication/)

# Motivation

* **Easy xCluster replication setup:** xCluster replication is currently a supported feature in Yugabyte DB. However, the only way to set it up is through a yb-admin command (CLI) which can be complicated and error-prone to use. 
* **Ensure setup correctness:** Currently, there is no easy way to find out if the replication is enabled or not for a universe or there is no way to track relationships between source and target universe.
* **Monitor and track xCluster replication:** There is no easy way to understand and monitor the overall configuration as well as the current state of replication. 

Our goal is to manage the xCluster replication setup, configuration, and monitoring through the Yugabyte Platform. 

# Usage

### Set up replication
* Platform will provide a way to configure target universe UUID, master addresses keyspace(list of tables), or namespace in replication
* Once target universe UUID and master addresses are entered, a list of keyspaces/namespace from the target universe would be pre-populated, and the users would be able to choose from the pre-populated list.
* If keyspace/namespace is added all the tables under it would be enabled for replication.
* Configure log retention duration (to handle target-source drift)

### Show configured replication
* **On source universes:** Users would be able to view a list of target universes and their names (or uuid) and master addresses. Users would also be able to view a list of tables that replication is set up on and replication lag per table level.
* **On target universes:** We would show universes that we are replicating from, source master addresses, and a list of tables being replicated.

### Start/Pause/Resume replication
* Users would be able to pause replication for maintenance activities. When paused, the replication on all the configured keyspaces/namespace in the universe will stop replicating to target universe.
* Paused replication would be resumed from the last checkpoint. 

### Validate replication
* Users would be able to validate and ensure the setup is working fine. Things like the universes should be up and running, tables should be created 
* Universes should indicate if the replication is enabled to them and what’s the target universes
* It would also perform schema validation of all the tables on the source side and ensure that they match the tables on the target side.
* Lag validation would be included.

### Change replication
* Using Platform UI, Users would be able to change the master addresses, ports, log retention duration (to handle target-source drift). 
* Note - we should explore if this can be done automatically without the user’s intervention, can master-nodes be auto discoverable?
* Changing keyspace/namespace, table schema would be applied through CLI. Any changes done through CLI would reflect on the Platform UI immediately.

### Bootstrapping target universe
Bootstrapping is intended to run once to replicate the base state of the databases from source to target universe. During bootstrap, all of the data from the source universe is copied to the target universe. 

When you configure xCluster replication between an existing old source universe and a newly created target universe, the data from the source universe has not been previously replicated and you need to perform bootstrap operation as mentioned in the following steps. 

1. First, we need to create a checkpoint on the source universe for all the tables we want to replicate.
2. Then, backup the tables on the source universe and restore to the target universe.
3. Finally, set up the replication and start it.

After the bootstrap operation succeeds, an incremental replication is automatically performed. This synchronizes, between the source and target universe, any events that occurred during the bootstrap process. After the data is synchronized, the replicated data is ready for use in the target universe. Data is in a consistent state only after incremental replication has captured any new changes that occurred during bootstrap.

### Backup and restore universes 
* For universes with xCluster replication enabled, backup and restore would be the same as the regular universe, including configuring backup storage, backup schedules, and restore operation.   
* Backup taken on the source universe can be restored in target universes

### Monitoring and Alerting
* Users would be able to view a complete picture of “source-target” relationship between two universes on the dashboard along with the list of tables and corresponding lag metrics. Max lag of 0 indicates the target universe has caught up with the source universe.
* This status information will be shown on both source and target universes. 
* Alerts for both table and universe level will be raised.
* In addition to replication default alerts, users would be able to create their alerts and pick a notification channel through which they like to get notified.
* For overall details refer [Platform Alerting and Notification](https://github.com/ymahajan/yugabyte-db/blob/current-roadmap-updates/architecture/design/platform-alerting-and-notification.md)

# Future Work
* Replicating DDL changes: Allow safe DDL changes to be propagated automatically to target universes.
* Apply schema changes through Platform: Support for any database operations such as DDLs, schema, keyspaces changes through Platform, all those YSQL/YCQL table- related operations are expected to be performed out of band.
* Automatically bootstrapping target universes: Currently, It is the responsibility of the end-user to ensure that a target universe has sufficiently recent updates so that replication can safely resume.
* Initialize replication: When adding a new table to be replicated, it should seed the target universe automatically.
* Provide time estimate to init replication
* Ability to manage replication between universes managed by two different platforms.
* Universe or schema configured for replication should automatically create or drop objects in the target universe
* Platform should manage the movement of masters and adjust replication accordingly
* Add view filter, within table view, showing replication of tables


# References

* [xCluster replication documentation](https://docs.yugabyte.com/latest/architecture/docdb-replication/async-replication/)
* [xCluster replication design](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/multi-region-xcluster-async-replication.md)
* [xCluster replication configuration script](https://github.com/yugabyte/ats-helper/blob/main/yb_configure_2dc.sh)

[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/design/platform-xcluster-replication-management.md?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)
