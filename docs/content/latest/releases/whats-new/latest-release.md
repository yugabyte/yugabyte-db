---
title: What's new in the v2.7 latest release series
headerTitle: What's new in the v2.7 latest release series
linkTitle: v2.7 (latest)
description: Enhancements, changes, and resolved issues in the latest release series.
headcontent: Features, enhancements, and resolved issues in the latest release series.
image: /images/section_icons/quick_start/install.png
aliases:
  - /latest/releases/
menu:
  latest:
    parent: whats-new
    identifier: latest-release
    weight: 2585
isTocNested: true
showAsideToc: true 
---

{{< warning title="Use v2.7.1.1 or later" >}}

YugabyteDB version 2.7.1.1 contains an important fix for a bug in a downstream dependency (the `gperftools` package). This `tcmalloc` memory allocation bug could lead to process crashes. If you're using a previous 2.7 version, please upgrade as soon as possible.

Refer to [issue 8531](https://github.com/yugabyte/yugabyte-db/issues/8531) for details.

{{< /warning >}}

## v2.7.1.1 - May 25, 2021

**Build:** `2.7.1.1-b1`

### Downloads

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.7.1.1-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.7.1.1-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.7.1.1-b1
```

### New Features

#### Yugabyte Platform

#### Core Database

N/A

### Improvements

#### Yugabyte Platform

* [[5041](https://github.com/yugabyte/yugabyte-db/issues/5041)] Added a health check for total memory(RAM) usage. 
* [[6555](https://github.com/yugabyte/yugabyte-db/issues/6555)] Similar to scheduled backups, added an ability to provide retention duration for manual backups as well.
* [[6913](https://github.com/yugabyte/yugabyte-db/issues/6913)] For slow query monitoring, added an ability to reset stats with the new ‘Reset Stats’ button.
* [[6913](https://github.com/yugabyte/yugabyte-db/issues/6913)] [[6914](https://github.com/yugabyte/yugabyte-db/issues/6914)] Add ability to reset slow query data and hide slow queries.
* [[6914](https://github.com/yugabyte/yugabyte-db/issues/6914)] Added an ability to turn on query monitoring for slow queries. By default, query monitoring is turned off.
* [[7215](https://github.com/yugabyte/yugabyte-db/issues/7215)] Added an ability to select multiple backups for deletion rather than deleting individual backups. 
* [[7223](https://github.com/yugabyte/yugabyte-db/issues/7223)] [[7224](https://github.com/yugabyte/yugabyte-db/issues/7224)] Added a new “Show Universes” action in the Actions menu. This provides a way for users to see all the associated universes that are using a particular KMS config. We are now also showing the list of universes as a modal dialog box associated with the certificate. 
* [[7278](https://github.com/yugabyte/yugabyte-db/issues/7278)] [[7446](https://github.com/yugabyte/yugabyte-db/issues/7446)] Improved search usability for Live and Slow queries by adding autocomplete suggestions, better filtering and navigation.
* [[7726](https://github.com/yugabyte/yugabyte-db/issues/7726)] Health check now runs in parallel on all the universes rather than sequential.
* [[7799](https://github.com/yugabyte/yugabyte-db/issues/7799)] Added support for AWS GP3 volumes during universe creation from the Platform. The disk size and IOPS configuration for GP3 drives are configurable, whereas throughput is not configurable and is set to default value of 125MiB/sec.
* [[7913](https://github.com/yugabyte/yugabyte-db/issues/7913)] When upgrading a universe with read replica clusters, nodes in primary clusters are now always upgraded first, then read replica cluster nodes. 
* [[7967](https://github.com/yugabyte/yugabyte-db/issues/7967)] Added ‘Download logs’ action under Nodes tab.
* [[7970](https://github.com/yugabyte/yugabyte-db/issues/7970)] Added a new ‘Backup Type’ label to distinguish YSQL and YCQL scheduled backups. For Yedis back type this field will be an empty string.
* [[8038](https://github.com/yugabyte/yugabyte-db/issues/8038)] Default metrics button now points to the Prometheus metrics endpoint.
* [[8081](https://github.com/yugabyte/yugabyte-db/issues/8081)] Added support for searching certificates by universe name in the Encryption-at-Rest. If there are more than 10 certificates, the user has to use pagination to search one page at a time to find the right certificate.

#### Core Database

**Point-in-time restore progress**

* [[7126](https://github.com/yugabyte/yugabyte-db/issues/7126)] Add restore_snapshot_schedule to admin
* [[7126](https://github.com/yugabyte/yugabyte-db/issues/7126)] Add yb-admin commands to create and list snapshot schedules
* [[7126](https://github.com/yugabyte/yugabyte-db/issues/7126)] PITR: Cleanup not restored tables and tablets
* [[7126](https://github.com/yugabyte/yugabyte-db/issues/7126)] PITR: Cleanup outdated snapshots
* [[7126](https://github.com/yugabyte/yugabyte-db/issues/7126)] PITR: Correct history retention for newly added tablets
* [[7126](https://github.com/yugabyte/yugabyte-db/issues/7126)] PITR: Load snapshot schedules during bootstrap
* [[7126](https://github.com/yugabyte/yugabyte-db/issues/7126)] PITR: Restore deleted table
* [[7126](https://github.com/yugabyte/yugabyte-db/issues/7126)] PITR: Special history retention mechanism
* [[7126](https://github.com/yugabyte/yugabyte-db/issues/7126)] PITR: Take system catalog snapshot
* [[7126](https://github.com/yugabyte/yugabyte-db/issues/7126)] [[7135](https://github.com/yugabyte/yugabyte-db/issues/7135)] PITR: Restore table schema
* [[7137](https://github.com/yugabyte/yugabyte-db/issues/7137)] PITR: Provide ability to create snapshot schedule for YSQL database and YCQL keyspace

**TLS-related**

* [[6845](https://github.com/yugabyte/yugabyte-db/issues/6845)] [YSQL] Introduce the 'use_node_hostname_for_local_tserver' gflag to use DNS name instead of IP for local tserver connection
* [[7756](https://github.com/yugabyte/yugabyte-db/issues/7756)] Make Encryption at Rest Code Openssl 1.1.1 Compatible
* [[8052](https://github.com/yugabyte/yugabyte-db/issues/8052)] Add ability to configure cipher list and cipher suites.

**UI improvements**

* [docdb] Added a max_depth param to the mem-trackers view (#7903)
* [[7620](https://github.com/yugabyte/yugabyte-db/issues/7620)] Refactor scoped leader shared lock instantiation
* [[7199](https://github.com/yugabyte/yugabyte-db/issues/7199)] track and display heartbeat roundtrip time from each yb-tserver in yb-master UI (#7239)
* [[7543](https://github.com/yugabyte/yugabyte-db/issues/7543)] docdb: Add uptime into master home UI
* [[7484](https://github.com/yugabyte/yugabyte-db/issues/7484)] docdb - Sort the hosts of tablet replicas consistently in Admin UI
* [[7617](https://github.com/yugabyte/yugabyte-db/issues/7617)] docdb: Record and display disk usage by drive
* [[7647](https://github.com/yugabyte/yugabyte-db/issues/7647)] docdb: Adds Num SST Files to TS tablets view

**Performance improvements**

* [[7487](https://github.com/yugabyte/yugabyte-db/issues/7487)] docdb - Remove unnecessary Value decoding and TTL calculation in doc_reader.cc
* [[7661](https://github.com/yugabyte/yugabyte-db/issues/7661)] docdb - Run manually triggered compactions concurrently
* [[7798](https://github.com/yugabyte/yugabyte-db/issues/7798)] DocDB: Only the YB-Master Leader should refresh the tablespace info in memory
* [[7844](https://github.com/yugabyte/yugabyte-db/issues/7844)] Set tcmalloc max cache bytes for yb-master similar to the yb-tserver.
* [[7873](https://github.com/yugabyte/yugabyte-db/issues/7873)] docdb - Initialize block cache for master/sys_catalog
* [[7894](https://github.com/yugabyte/yugabyte-db/issues/7894)] Don't create long operation tracker for empty ScopedRWOperation
* [[8002](https://github.com/yugabyte/yugabyte-db/issues/8002)] docdb: Increase thresholds for master long lock warnings
* [[8015](https://github.com/yugabyte/yugabyte-db/issues/8015)] Remove tablets belonging to the same table by taking table lock once
* [[8037](https://github.com/yugabyte/yugabyte-db/issues/8037)] docdb - Refactor memory management for tablets into a separate class
* [[8071](https://github.com/yugabyte/yugabyte-db/issues/8071)] Lookup HostPort in blacklist for TSManager::IsTsBlacklisted
* [[8061](https://github.com/yugabyte/yugabyte-db/issues/8061)] Iterate over copy of namespace_ids_map_ when listing namespaces
* [[8133](https://github.com/yugabyte/yugabyte-db/issues/8133)] remove CatalogManager::CheckOnline()
* [[8167](https://github.com/yugabyte/yugabyte-db/issues/8167)] Properly scope namespace read lock in CatalogManager::CreateTable()
* [[8170](https://github.com/yugabyte/yugabyte-db/issues/8170)] ybase: Check dirty bit of tablet metadata before issuing removal
* [[8260](https://github.com/yugabyte/yugabyte-db/issues/8260)] ybase: Use shared lock when checking table truncation / deletion

**Tablet splitting**

* [[5854](https://github.com/yugabyte/yugabyte-db/issues/5854)] docdb: Handling tablet splitting errors at YBSession level
* [[6719](https://github.com/yugabyte/yugabyte-db/issues/6719)] docdb: Added YBOperation table_partition_list_version checking
* [[7108](https://github.com/yugabyte/yugabyte-db/issues/7108)] [docdb] Disable tablet splitting during index backfill
* [[8201](https://github.com/yugabyte/yugabyte-db/issues/8201)] docdb: YBSession API cleanup

**Load balancer and placement improvements**

* [[1479](https://github.com/yugabyte/yugabyte-db/issues/1479)] ybase: Allow normal load balancing for DEAD+BLACKLISTED TS
* [[3040](https://github.com/yugabyte/yugabyte-db/issues/3040)] ybase: Allow global leader load balancing
* [[6631](https://github.com/yugabyte/yugabyte-db/issues/6631)] ybase: Allow support for prefixes while specifying placement info
* [[6947](https://github.com/yugabyte/yugabyte-db/issues/6947)] ybase: Allow leader balancing for DEAD nodes
* [[7369](https://github.com/yugabyte/yugabyte-db/issues/7369)] ysql: Respect leader affinity on master sys catalog tablet

**T-server memory overhead**

* [[7804](https://github.com/yugabyte/yugabyte-db/issues/7804)] docdb: Make WritableFileWriter buffer gflag controllable
* [[7805](https://github.com/yugabyte/yugabyte-db/issues/7805)] Share Histograms across tablets belonging to a table instead of having Histograms (in TabletMetrics and other objects) separately for each tablet.
* [[8073](https://github.com/yugabyte/yugabyte-db/issues/8073)] Drop rocksdb memstore arena from 128kb to 64kb

**YCQL deferred index backfill**

* [[8069](https://github.com/yugabyte/yugabyte-db/issues/8069)] YCQL: Basic support for deferred/batched index backfill
* [[6290](https://github.com/yugabyte/yugabyte-db/issues/6290)] [[8069](https://github.com/yugabyte/yugabyte-db/issues/8069)] Pt 2 & 3: CQL Handle partial-failures in a batch of index backfills.
* [[8069](https://github.com/yugabyte/yugabyte-db/issues/8069)] Add a yb-admin command to backfill tables with deferred indexes

**Other core database improvements**

* [[1248](https://github.com/yugabyte/yugabyte-db/issues/1248)] ysql: Create background task for verifying tablet data integrity
* [[3460](https://github.com/yugabyte/yugabyte-db/issues/3460)] YSQL: Integrate Orafce extension with Yugabyte
* [[4580](https://github.com/yugabyte/yugabyte-db/issues/4580)] add metric for wal files size (#7260)
* [[4934](https://github.com/yugabyte/yugabyte-db/issues/4934)] [[7922](https://github.com/yugabyte/yugabyte-db/issues/7922)] Thread safety improvements in the Transaction class
* [[6636](https://github.com/yugabyte/yugabyte-db/issues/6636)] docdb: Cache table->tablespace->placement information in YB-Master
* [[6672](https://github.com/yugabyte/yugabyte-db/issues/6672)] docdb: added explicit initialization of libbacktrace into InitYB.
* [[7068](https://github.com/yugabyte/yugabyte-db/issues/7068)] Allow reloading of the config file with 'ts-cli'
* [[7324](https://github.com/yugabyte/yugabyte-db/issues/7324)] YSQL: Early bailout when bind condition is an empty search array
* [[7557](https://github.com/yugabyte/yugabyte-db/issues/7557)] YCQL: Support != operator
* [[7564](https://github.com/yugabyte/yugabyte-db/issues/7564)] ybase: Auto tune ysql_num_shards_per_tserver similar to yb_num_shards_per_tserver
* [[7632](https://github.com/yugabyte/yugabyte-db/issues/7632)] ycql: Support upsert for jsonb column field values
* [[7724](https://github.com/yugabyte/yugabyte-db/issues/7724)] ysql: add GUC var yb_index_state_flags_update_delay
* [[7916](https://github.com/yugabyte/yugabyte-db/issues/7916)] CQL call timeout
* [[7937](https://github.com/yugabyte/yugabyte-db/issues/7937)] YSQL: Avoid unnecessary secondary index writes for UPDATE on table with
* [[7977](https://github.com/yugabyte/yugabyte-db/issues/7977)] [docdb] Send per tablet disk usage to the master via heartbeats
* [[8026](https://github.com/yugabyte/yugabyte-db/issues/8026)] Bump up timestamp_history_retention_interval_sec to 900s
* [[8027](https://github.com/yugabyte/yugabyte-db/issues/8027)] A separate YSQL flag for yb_client_timeout

### Bug Fixes

#### Yugabyte Platform

* [[5246](https://github.com/yugabyte/yugabyte-db/issues/5246)] Fixed cluster_health to examine only local volumes and exclude nfs from consideration so that false alerts are not generated.
* [[5733](https://github.com/yugabyte/yugabyte-db/issues/5733)] Disabled "stop process" and "remove node" for a single node universe
* [[5946](https://github.com/yugabyte/yugabyte-db/issues/5946)] Clock sync is now checked while creating or expanding the universe. Clock sync is added to health checks now.
* [[6019](https://github.com/yugabyte/yugabyte-db/issues/6019)] Added an init container to yugabyte helm charts to wait for container to be ready
* [[6924](https://github.com/yugabyte/yugabyte-db/issues/6924)] When a node is removed/released from a universe, hide the "Show Live Queries" button.
* [[7007](https://github.com/yugabyte/yugabyte-db/issues/7007)] Fixed an issue where Restore backup dialog allowed empty/no universe name selected.
* [[7171](https://github.com/yugabyte/yugabyte-db/issues/7171)] Added a validation that on-prem instance type name cannot be same for different customers on the same platform.
* [[7172](https://github.com/yugabyte/yugabyte-db/issues/7172)] Added visual feedback for certain universe creation failures such as pre-flight validation failures or bad request response from API, etc.
* [[7193](https://github.com/yugabyte/yugabyte-db/issues/7193)] Fixed issues with Run sample apps to have the deterministic payload and unify behaviour of YCQL and YSQL app. 
* [[7311](https://github.com/yugabyte/yugabyte-db/issues/7311)] Added appropriate warnings while using ephemeral storage for the cases like stopping a VM or pausing an universe as it will potentially lead to data loss.
* [[7408](https://github.com/yugabyte/yugabyte-db/issues/7408)] Retry Task button should not be visible for tasks other than "Create Universe" Task, as it’s the only task that supports retry.
* [[7412](https://github.com/yugabyte/yugabyte-db/issues/7412)] Make footer link buttons clickable
* [[7415](https://github.com/yugabyte/yugabyte-db/issues/7415)] Made secure the default configuration of SSH daemon by avoiding password authentication and PermitRootLogin in VMs
* [[7416](https://github.com/yugabyte/yugabyte-db/issues/7416)] Platform: Changed default port of On-Prem provider to 22 (#7599)
* [[7421](https://github.com/yugabyte/yugabyte-db/issues/7421)] Encryption is enabled by default for both client to node and node to node cases.
* [[7432](https://github.com/yugabyte/yugabyte-db/issues/7432)] In the case of the AWS provider, fixing an issue of ssh key name and private key were getting ignored.
* [[7437](https://github.com/yugabyte/yugabyte-db/issues/7437)] Since Kubernetes currently doesn't support read replicas, disabled it from the UI; k8s providers are also not shown when configuring a read-replica.
* [[7441](https://github.com/yugabyte/yugabyte-db/issues/7441)] Added field-level validation for User Tags to disallow "Name" as a key for a tag
* [[7442](https://github.com/yugabyte/yugabyte-db/issues/7442)] Only include the queries run by the user under slow queries
* [[7444](https://github.com/yugabyte/yugabyte-db/issues/7444)] Fixed an issue in Edit Universe, as user was able to edit User Tags but not save them
* [[7447](https://github.com/yugabyte/yugabyte-db/issues/7447)] When universe creation is in progress, other operations which require the Universe in "ready" state should be disabled like "Edit universe", "Read replicas", "Run sample apps", etc.
* [[7536](https://github.com/yugabyte/yugabyte-db/issues/7536)] You can now specify an SSH username even when not using a custom key-pair.
* [[7554](https://github.com/yugabyte/yugabyte-db/issues/7554)] Fixed an issue where error toaster appears even when the Provider is added successfully
* [[7561](https://github.com/yugabyte/yugabyte-db/issues/7561)] [[7699](https://github.com/yugabyte/yugabyte-db/issues/7699)] [7717] Fixed an issue in trying to force-delete a universe and you will be redirected to an error page on success.
* [[7562](https://github.com/yugabyte/yugabyte-db/issues/7562)] In case of Encryption at rest configuration fixed an error in configuring KMS provider. 
* [[7591](https://github.com/yugabyte/yugabyte-db/issues/7591)] Added labeling for the Azure Instance Type dropdown similar to GCP/AWS.
* [[7624](https://github.com/yugabyte/yugabyte-db/issues/7624)] Removed refetch on window focus for slow queries
* [[7656](https://github.com/yugabyte/yugabyte-db/issues/7656)] After manually provisioning an on-premises node, create universe tries to use "centos" user, not "yugabyte"
* [[7659](https://github.com/yugabyte/yugabyte-db/issues/7659)] Non-replicated flow fails due to package requiring python3
* [[7672](https://github.com/yugabyte/yugabyte-db/issues/7672)] Cannot read property 'data' of undefined on Tasks -> Retry Task
* [[7687](https://github.com/yugabyte/yugabyte-db/issues/7687)] YSQL health check fails when YSQL auth is enabled
* [[7698](https://github.com/yugabyte/yugabyte-db/issues/7698)] Custom SMTP Configuration API returns unmasked SMTP password
* [[7703](https://github.com/yugabyte/yugabyte-db/issues/7703)] Can't send email for custom SMTP settings without authentication (empty username)
* [[7704](https://github.com/yugabyte/yugabyte-db/issues/7704)] Backup to S3 fails using Yugaware instance's IAM role
* [[7727](https://github.com/yugabyte/yugabyte-db/issues/7727)] [[7728](https://github.com/yugabyte/yugabyte-db/issues/7728)] Fix UI issues with k8s provider creation and deletion
* [[7736](https://github.com/yugabyte/yugabyte-db/issues/7736)] Change the username help info for certificate based authentication
* [[7740](https://github.com/yugabyte/yugabyte-db/issues/7740)] Prometheus going down silently after YW upgrade thru replicated
* [[7769](https://github.com/yugabyte/yugabyte-db/issues/7769)] Prevent adding on-prem node instance with duplicate IP
* [[7779](https://github.com/yugabyte/yugabyte-db/issues/7779)] Health check fails on k8s portal for all the universes on clock synchronization with FailedClock synchronization and Error getting NTP state 
* [[7780](https://github.com/yugabyte/yugabyte-db/issues/7780)] Fixed an issue causing old backups to not get deleted by a schedule.
* [[7810](https://github.com/yugabyte/yugabyte-db/issues/7810)] Health check emails not working with default SMTP configuration
* [[7811](https://github.com/yugabyte/yugabyte-db/issues/7811)] Slow queries is not displaying all queries on k8s universe pods
* [[7908](https://github.com/yugabyte/yugabyte-db/issues/7908)] Added a fix caused while deleting a universe with a stopped node
* [[7909](https://github.com/yugabyte/yugabyte-db/issues/7909)] Fix issue with signature could not be verified for google-cloud-sdk for GCP VMs
* [[7950](https://github.com/yugabyte/yugabyte-db/issues/7950)] Navigating to a universe with KMS enabled will show this error due if something has been misconfigured
* [[7959](https://github.com/yugabyte/yugabyte-db/issues/7959)] Disabling Node-to-Node TLS during universe creation causes universe creation to fail
* [[7988](https://github.com/yugabyte/yugabyte-db/issues/7988)] Backup deletion failure should not cause retries
* [[8020](https://github.com/yugabyte/yugabyte-db/issues/8020)] To handle auth token expiration, added a global interceptor to catch all 403-code responses and redirect to the login page with a session expiration message in error toast. So when auth token expires background API calls won't fail silently anymore.
* [[8051](https://github.com/yugabyte/yugabyte-db/issues/8051)] Redact sensitive data and secrets from audit logs
* [[8176](https://github.com/yugabyte/yugabyte-db/issues/8176)] Fixed an issue of setting the max number of processes properly when deploying universes in GCP 
* [[8189](https://github.com/yugabyte/yugabyte-db/issues/8189)] Make sure AWS instances with no EBS volume do not pass "Scratch" as default value
* [[8243](https://github.com/yugabyte/yugabyte-db/issues/8243)] Make Persistent storage default for GCP universe

#### Core Database

* [[2977](https://github.com/yugabyte/yugabyte-db/issues/2977)] docdb: CountIntents() should use shared tablet pointer
* [[4250](https://github.com/yugabyte/yugabyte-db/issues/4250)] ybase: Stop Load balancing for deleting/deleted tables
* [[4412](https://github.com/yugabyte/yugabyte-db/issues/4412)] docdb: Fix LB State for Move Operations
* [[5380](https://github.com/yugabyte/yugabyte-db/issues/5380)] Add re-try in postgres build when encountering transient error.
* [[6096](https://github.com/yugabyte/yugabyte-db/issues/6096)] ysql: Fix crash during bootstrap when replaying WAL of deleted colocated table
* [[6615](https://github.com/yugabyte/yugabyte-db/issues/6615)] [[7693](https://github.com/yugabyte/yugabyte-db/issues/7693)] [YCQL] Manifest generation for YCQL command for sample apps. kubectl command and docker command to run sample apps will be created automatically.
* [[6672](https://github.com/yugabyte/yugabyte-db/issues/6672)] docdb: fix for deadlock in GlobalBacktraceState constructor
* [[6789](https://github.com/yugabyte/yugabyte-db/issues/6789)] YSQL: Fix ysql_dumpall and ysql_dump to work with Tablespaces
* [[6821](https://github.com/yugabyte/yugabyte-db/issues/6821)] [[7069](https://github.com/yugabyte/yugabyte-db/issues/7069)] [7344] (YCQL) Fix issues when selecting optimal scan path
* [[6951](https://github.com/yugabyte/yugabyte-db/issues/6951)] [YSQL] Fix missed check of YBStatus
* [[6972](https://github.com/yugabyte/yugabyte-db/issues/6972)] Update local limit in case of a successful read
* [[6982](https://github.com/yugabyte/yugabyte-db/issues/6982)] [YSQL] Specify read time for catalog tables to guarantee consistent state of catalog cache
* [[7047](https://github.com/yugabyte/yugabyte-db/issues/7047)] [YSQL] Read minimal possible number of columns in case of index scan
* [[7355](https://github.com/yugabyte/yugabyte-db/issues/7355)] ysql: check backfill bad connection status
* [[7390](https://github.com/yugabyte/yugabyte-db/issues/7390)] Preflight checks should handle sudo passwords when given
* [[7390](https://github.com/yugabyte/yugabyte-db/issues/7390)] Rename V65 migration with R prefix
* [[7398](https://github.com/yugabyte/yugabyte-db/issues/7398)] docdb - Crashing after CopyTo from parent to child causes child bootstrap failure
* [[7398](https://github.com/yugabyte/yugabyte-db/issues/7398)] docdb - Forcing remote bootstrap to replay split operation causes seg fault
* [[7455](https://github.com/yugabyte/yugabyte-db/issues/7455)] [YCQL] Update index from transaction with cross-key statements.
* [[7499](https://github.com/yugabyte/yugabyte-db/issues/7499)] ysql: Import pg_dump: label INDEX ATTACH ArchiveEntries with an owner.
* [[7534](https://github.com/yugabyte/yugabyte-db/issues/7534)] YSQL: Support ALTER TABLE ADD PRIMARY KEY for colocated tables
* [[7547](https://github.com/yugabyte/yugabyte-db/issues/7547)] Set flags automatically based on the node's available resources
* [[7600](https://github.com/yugabyte/yugabyte-db/issues/7600)] [YSQL] Explain --masters in ysql_dump cli.
* [[7602](https://github.com/yugabyte/yugabyte-db/issues/7602)] docdb: FlushTablets rpc causes SEGV of the tserver process
* [[7603](https://github.com/yugabyte/yugabyte-db/issues/7603)] rocksdb: Calling memset on atomic variable generates warning (#7604)
* [[7628](https://github.com/yugabyte/yugabyte-db/issues/7628)] Add ldap libraries as special case for yb client packaging
* [[7641](https://github.com/yugabyte/yugabyte-db/issues/7641)] YCQL: Fix checks in index update path that determine full row removal.
* [[7649](https://github.com/yugabyte/yugabyte-db/issues/7649)] YCQL: Block secondary index creation on static columns.
* [[7651](https://github.com/yugabyte/yugabyte-db/issues/7651)] YSQL: Always listen on UNIX domain socket
* [[7678](https://github.com/yugabyte/yugabyte-db/issues/7678)] ysql: Import Fix race condition in psql \e's detection of file modification.
* [[7682](https://github.com/yugabyte/yugabyte-db/issues/7682)] ysql: Import Forbid marking an identity column as nullable.
* [[7702](https://github.com/yugabyte/yugabyte-db/issues/7702)] ysql: Import Avoid corner-case memory leak in SSL parameter processing.
* [[7705](https://github.com/yugabyte/yugabyte-db/issues/7705)] ysql: prioritize internal HBA config
* [[7715](https://github.com/yugabyte/yugabyte-db/issues/7715)] YSQL: Prevent DocPgsqlScanSpec and DocQLScanSpec from accepting rvalue reference to hash and range components
* [[7729](https://github.com/yugabyte/yugabyte-db/issues/7729)] Avoid recreating aborted transaction
* [[7729](https://github.com/yugabyte/yugabyte-db/issues/7729)] Fix checking ABORTED txn status at follower
* [[7741](https://github.com/yugabyte/yugabyte-db/issues/7741)] ysql: Import Don't leak malloc'd strings when a GUC setting is rejected.
* [[7748](https://github.com/yugabyte/yugabyte-db/issues/7748)] YSQL: ALTER ADD PK should do column checks
* [[7791](https://github.com/yugabyte/yugabyte-db/issues/7791)] ysql: Import Fix psql's \connect command some more.
* [[7802](https://github.com/yugabyte/yugabyte-db/issues/7802)] ysql: Import Fix connection string handling in psql's \connect command.
* [[7806](https://github.com/yugabyte/yugabyte-db/issues/7806)] ysql: Import Fix recently-introduced breakage in psql's \connect command.
* [[7812](https://github.com/yugabyte/yugabyte-db/issues/7812)] ysql: Import Fix connection string handling in src/bin/scripts/ programs.
* [[7813](https://github.com/yugabyte/yugabyte-db/issues/7813)] [YSQL] YSQL dump should always include HASH/ASC/DESC modifier for indexes/pkey.
* [[7835](https://github.com/yugabyte/yugabyte-db/issues/7835)] Don't crash when trying to append ValueType::kTombstone to a key
* [[7848](https://github.com/yugabyte/yugabyte-db/issues/7848)] Fix for yb-prof for python3
* [[7872](https://github.com/yugabyte/yugabyte-db/issues/7872)] Remove flashcache-related code
* [[7894](https://github.com/yugabyte/yugabyte-db/issues/7894)] Keep ScopedRWOperation while applying intents for large transaction
* [[7939](https://github.com/yugabyte/yugabyte-db/issues/7939)] Enforce password policy
* [[7940](https://github.com/yugabyte/yugabyte-db/issues/7940)] docdb: Unregister BlockBasedTable memtrackers from parent on tablet deletion
* [[7944](https://github.com/yugabyte/yugabyte-db/issues/7944)] ysql: deprecate flag ysql_wait_until_index_permissions_timeout_ms
* [[7979](https://github.com/yugabyte/yugabyte-db/issues/7979)] ysql Import Fix handling of -d "connection string" in pg_dump/pg_restore.
* [[8065](https://github.com/yugabyte/yugabyte-db/issues/8065)] docdb: Fix Sys Catalog Leader Affinity with Full Move
* [[8006](https://github.com/yugabyte/yugabyte-db/issues/8006)] ysql: Import Fix out-of-bound memory access for interval -> char conversion
* [[8024](https://github.com/yugabyte/yugabyte-db/issues/8024)] YSQL: Redundant read for create table with primary key
* [[8030](https://github.com/yugabyte/yugabyte-db/issues/8030)] ysql: Import Redesign the caching done by get_cached_rowtype().
* [[8047](https://github.com/yugabyte/yugabyte-db/issues/8047)] ysql: Import Fix some inappropriately-disallowed uses of ALTER ROLE/DATABASE SET.
* [[8079](https://github.com/yugabyte/yugabyte-db/issues/8079)] Ensure leadership before handling catalog version
* [[8101](https://github.com/yugabyte/yugabyte-db/issues/8101)] YCQL: Fixed CQLServiceImpl::Shutdown
* [[8102](https://github.com/yugabyte/yugabyte-db/issues/8102)] Traverse pgsql_ops_ once in PgDocOp::ProcessResponseResult()
* [[8112](https://github.com/yugabyte/yugabyte-db/issues/8112)] Correct string substitution in UpdateTablet()
* [[8114](https://github.com/yugabyte/yugabyte-db/issues/8114)] [YSQL] [backup] Partial index syntax error in the ysql_dump output
* [[8118](https://github.com/yugabyte/yugabyte-db/issues/8118)] ysql: Import 'Fix memory leak when rejecting bogus DH parameters.'
* [[8119](https://github.com/yugabyte/yugabyte-db/issues/8119)] ycql: Properly compare BigDecimals for equality
* [[8150](https://github.com/yugabyte/yugabyte-db/issues/8150)] [[8196](https://github.com/yugabyte/yugabyte-db/issues/8196)] Fix preceding op id in case of empty ops sent to the follower
* [[8183](https://github.com/yugabyte/yugabyte-db/issues/8183)] ysql: Import Always call ExecShutdownNode() if appropriate.
* [[8225](https://github.com/yugabyte/yugabyte-db/issues/8225)] YQL: Add missing makefile dependency
* [[8496](https://github.com/yugabyte/yugabyte-db/issues/8496)] Downgrade gperftools to 2.7

### Known Issues

#### Yugabyte Platform

N/A

#### Core Database

N/A

## v2.7.0 - May 5, 2021

Yugabyte release 2.7.0 builds on our work in the 2.5 series, which fed into the 2.4 stable release. With release 2.7.0, we're planning development on a number of [new features](#new-features), as well as refinements to existing functionality.

**Build:** `2.7.0.0-b17`

### Downloads

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.7.0.0-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.7.0.0-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.7.0.0-b17
```

### New Features

#### Yugabyte Platform

* Yugabyte Platform High Availability is now available in BETA. With this feature, you can deploy multiple platforms in an active-standby cluster, ensuring you can always monitor and manage your platform-managed universes regardless of outages to any particular platform in the HA cluster.
* [[7225](https://github.com/yugabyte/yugabyte-db/issues/7225)] [[7228](https://github.com/yugabyte/yugabyte-db/issues/7228)] [UI] Enable the OpenShift tab, marked as beta

#### Core Database

* [[2717](https://github.com/yugabyte/yugabyte-db/issues/2717)] YSQL: Support ALTER FUNCTION command
* [[6985](https://github.com/yugabyte/yugabyte-db/issues/6985)] YSQL: Add simple UNIQUE column
* **Point in time restore**:
  * [[7121](https://github.com/yugabyte/yugabyte-db/issues/7121)] Extend yb-admin restore_snapshot to use a custom time
  * [[7126](https://github.com/yugabyte/yugabyte-db/issues/7126)] PITR: Introduce snapshot schedule

### Improvements

#### Yugabyte Platform

* [[5535](https://github.com/yugabyte/yugabyte-db/issues/5535)] [Platform] Remove stale instance configs after cloud provider is deleted (#6975)
* [[5624](https://github.com/yugabyte/yugabyte-db/issues/5624)] Show pricing info for Azure
* [[5626](https://github.com/yugabyte/yugabyte-db/issues/5626)] Enable Hosted Zone for Azure
* [[5628](https://github.com/yugabyte/yugabyte-db/issues/5628)] [Azure] Support regions with no availability zones
* [[5807](https://github.com/yugabyte/yugabyte-db/issues/5807)] Add toggle to show deleted backups
* [[5838](https://github.com/yugabyte/yugabyte-db/issues/5838)] [Platform] Add node link to corresponding Azure portal URL
* [[5840](https://github.com/yugabyte/yugabyte-db/issues/5840)] Add user tags for Azure universes
* [[5841](https://github.com/yugabyte/yugabyte-db/issues/5841)] Show only Azure VMs that meet minimum requirements
* [[6321](https://github.com/yugabyte/yugabyte-db/issues/6321)] [Platform] Support using Shared Gallery Images when creating Azure universes
* [[6681](https://github.com/yugabyte/yugabyte-db/issues/6681)] [Platform] Show kubectl command for Kubernetes pods in Connect modal (#7506)
* [[6712](https://github.com/yugabyte/yugabyte-db/issues/6712)] Fix issue with JDK incompatibility in Java RPC client
* [[6756](https://github.com/yugabyte/yugabyte-db/issues/6756)] [Platform] Created date showing up as "Invalid Date". (#7158)
* [[7024](https://github.com/yugabyte/yugabyte-db/issues/7024)] [Platform] Unable to edit number of nodes in AZ section (#7350)
* [[7054](https://github.com/yugabyte/yugabyte-db/issues/7054)] [YW] Add conditional checks for hiding specific platform elements in non-platform mode.
* [[7372](https://github.com/yugabyte/yugabyte-db/issues/7372)] [Platform] Skip running periodic schedules when in follower mode
* [[7433](https://github.com/yugabyte/yugabyte-db/issues/7433)] [Platform] Standby instances backup time not consistent after restoring active instance
* [[7443](https://github.com/yugabyte/yugabyte-db/issues/7443)] [Platform] Fixed live query details side panel doesn’t go away after unchecked
* [[7445](https://github.com/yugabyte/yugabyte-db/issues/7445)] [Platform] Add pagination for slow queries
* [[7472](https://github.com/yugabyte/yugabyte-db/issues/7472)] [Platform] Hide the Upgrade button from info card on Pause universe. (#7504)
* [[7475](https://github.com/yugabyte/yugabyte-db/issues/7475)] Use more recent CentOS-7 base image for GCP universe VMs
* [[7493](https://github.com/yugabyte/yugabyte-db/issues/7493)] [Platform] Menu shows 0 appended to "Upgrade Software0" - Removed 0
* [[7548](https://github.com/yugabyte/yugabyte-db/issues/7548)] [Platform] Set versions for google modules in requirements.txt
* [[7549](https://github.com/yugabyte/yugabyte-db/issues/7549)] [Platform] Platform uses public IP instead of private IP to connect to Azure universes
* [[7576](https://github.com/yugabyte/yugabyte-db/issues/7576)] Ensure rsync is available on latest GCP image
* [Platform] Use more recent CentOS-7 base image for GCP universe VMs #7475
* [Platform] parsing of df output is fragile and may fail in case of "safe" error in df #7402

#### Core Database

* [[4944](https://github.com/yugabyte/yugabyte-db/issues/4944)] YSQL performance improvements in TTL-related computations
* [[5922](https://github.com/yugabyte/yugabyte-db/issues/5922)] YCQL: Improve audit logging
* [[7359](https://github.com/yugabyte/yugabyte-db/issues/7359)] YSQL: Support adding primary key to a table with tablespace
* [[7366](https://github.com/yugabyte/yugabyte-db/issues/7366)] YSQL: Allow getting current SQL query in pggate for debug logging
* [[7404](https://github.com/yugabyte/yugabyte-db/issues/7404)] YSQL: Extend CHECKPOINT to have beta noop functionality
* [[7418](https://github.com/yugabyte/yugabyte-db/issues/7418)] [[7463](https://github.com/yugabyte/yugabyte-db/issues/7463)] YSQL: Import the 'force' option for the Drop Database command
* [[7532](https://github.com/yugabyte/yugabyte-db/issues/7532)] Master performance improvements: reduce the scope of catalog manager state lock in ScopedLeaderSharedLock constructor

### Bug Fixes

#### Core Database

* [[5383](https://github.com/yugabyte/yugabyte-db/issues/5383)] YSQL bug fixes for Jepsen

### Known Issues

#### Yugabyte Platform

N/A

#### Core Database

N/A

## Notes

{{< note title="New release versioning" >}}

Starting with v2.2, Yugabyte release versions follow a [new release versioning convention](../../versioning). The latest release series, denoted by `MAJOR.ODD`, incrementally introduces new features and changes and is intended for development and testing only. Revision releases, denoted by `MAJOR.ODD.REVISION` versioning, can include new features and changes that might break backwards compatibility. For more information, see [Supported and planned releases](../../releases-overview).

{{< /note >}}
