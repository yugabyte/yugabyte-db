---
title: What's new in the v2.5 latest release series
headerTitle: What's new in the v2.5 latest release series
linkTitle: v2.5 (latest)
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

Included here are the release notes for all releases in the v2.5 latest release series.

## Notable features and changes (cumulative for the v2.5 latest release series)

Note: Content will be added as new notable features and changes are available in the patch releases of the v2.5 latest release series.

## v2.5.2 - Feb 23, 2021

**Build:** `2.5.2.0-b104`

### Downloads

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.5.2.0-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.5.2.0-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.5.2.0-b104
```

### Improvements

#### Yugabyte Platform

* [[5376](https://github.com/yugabyte/yugabyte-db/issues/5376)] Added EULA in user registration flow during Platform installation
* [[5732](https://github.com/yugabyte/yugabyte-db/issues/5732)] Disable "Delete provider" for on-premise provider with active universes
* [[5836](https://github.com/yugabyte/yugabyte-db/issues/5836)] Package managed python packages in a portable manner
* [[5919](https://github.com/yugabyte/yugabyte-db/issues/5919)] Platform: change default storage option for AWS provider to 1x250GB
* [[6017](https://github.com/yugabyte/yugabyte-db/issues/6017)] Platform: Need ability for platform to check for actual clock synchronization
* [[6035](https://github.com/yugabyte/yugabyte-db/issues/6035)] Platform UI: show region level config paths for k8s providers
* [[6084](https://github.com/yugabyte/yugabyte-db/issues/6084)] Platform UI: disable STOP and REMOVE actions for kubernetes pods
* [[6165](https://github.com/yugabyte/yugabyte-db/issues/6165)] Platform: Renamed "Universe backups" label for keyspace -or- namespace level backups.
* [[6230](https://github.com/yugabyte/yugabyte-db/issues/6230)] Platform: Add tooltips to KMS Configuration UI
* [[6346](https://github.com/yugabyte/yugabyte-db/issues/6346)] UI visual feedback for KMS related tasks
* [[6421](https://github.com/yugabyte/yugabyte-db/issues/6421)] Edit Universe fails silently in case when it has decommissioned node(s)
* [[6477](https://github.com/yugabyte/yugabyte-db/issues/6477)] UI validation for duplicate region codes
* [[6540](https://github.com/yugabyte/yugabyte-db/issues/6540)] Add ability to remove certificates from UI
* [[6655](https://github.com/yugabyte/yugabyte-db/issues/6655)] Platform: Enabled AWS timeSync as a default on AWS universe
* [[6680](https://github.com/yugabyte/yugabyte-db/issues/6680)] [[6882](https://github.com/yugabyte/yugabyte-db/issues/6882)] Platform: Disabled deleting storage configuration when it is in use
* [[6708](https://github.com/yugabyte/yugabyte-db/issues/6708)] For AWS universes fetch instance types with respect to selected availability zones
* [[6740](https://github.com/yugabyte/yugabyte-db/issues/6740)] Update clients to support mTLS in YB clusters.
* [[6807](https://github.com/yugabyte/yugabyte-db/issues/6807)] Platform: Kubernetes: Allow specifying namespace in the AZ config
* [[6838](https://github.com/yugabyte/yugabyte-db/issues/6838)] [[6839](https://github.com/yugabyte/yugabyte-db/issues/6839)] [YW] Remove YSQL transactions metrics from Metrics tab
* [[6858](https://github.com/yugabyte/yugabyte-db/issues/6858)] Platform: Preflight checks for NFS backup storage configuration
* [[6893](https://github.com/yugabyte/yugabyte-db/issues/6893)] Platform: Allow platform to specify min TLS version for its own frontend

#### Core Database

* [[1104](https://github.com/yugabyte/yugabyte-db/issues/1104)] YSQL: ALTER TABLE ADD PRIMARY KEY
* [[3565](https://github.com/yugabyte/yugabyte-db/issues/3565)] tools: Allow developers to run yugabyted from the repo
* [[4230](https://github.com/yugabyte/yugabyte-db/issues/4230)] tools: Multi-node cluster restart support for yugabyted
* [[5686](https://github.com/yugabyte/yugabyte-db/issues/5686)] YSQL: apply empty deletes sparingly
* [[5716](https://github.com/yugabyte/yugabyte-db/issues/5716)] Enable Master Register From Raft by Default
* [[6128](https://github.com/yugabyte/yugabyte-db/issues/6128)] 2dc: Enable Replicate Intents By Default
* [[6215](https://github.com/yugabyte/yugabyte-db/issues/6215)] YSQL: Support index backfill on colocated tables
* [[6511](https://github.com/yugabyte/yugabyte-db/issues/6511)] YSQL: the WITH clause is now supported
* [[6571](https://github.com/yugabyte/yugabyte-db/issues/6571)] Handle read replicas when checking counts in GetTabletLocations
* [[6632](https://github.com/yugabyte/yugabyte-db/issues/6632)] ybase: Add new flag for how many servers to wait for before the transactions table is
* [[6636](https://github.com/yugabyte/yugabyte-db/issues/6636)] docdb: Cache table->tablespace->placement information in YB-Master
* [[6637](https://github.com/yugabyte/yugabyte-db/issues/6637)] YSQL: Support CREATE TABLE/INDEX <table/index> SET TABLESPACE <tablespace>
* [[6638](https://github.com/yugabyte/yugabyte-db/issues/6638)] YSQL: Support DROP TABLESPACE API
* [[6720](https://github.com/yugabyte/yugabyte-db/issues/6720)] YSQL: pg_hint_plan is now available. This allows you to adjust the query execution plan with hinting phrases in specially formatted comments.
* [[6749](https://github.com/yugabyte/yugabyte-db/issues/6749)] YSQL: Add support for FETCH NEXT, FORWARD, and positive row-count options
* [[6771](https://github.com/yugabyte/yugabyte-db/issues/6771)] YSQL: Add functionality to validate keys in a JSON object
* [[6772](https://github.com/yugabyte/yugabyte-db/issues/6772)] Display tserver clock information in yb-master UI
* [[6784](https://github.com/yugabyte/yugabyte-db/issues/6784)] YSQL: use all rather than 0.0.0.0/0, ::0/0 in hba conf (#7046)
* [[6825](https://github.com/yugabyte/yugabyte-db/issues/6825)] Add tablet server readiness to yb-ts-cli
* [[6833](https://github.com/yugabyte/yugabyte-db/issues/6833)] TLS Improvements: Add flag to skip client endpoint verification
* [[6841](https://github.com/yugabyte/yugabyte-db/issues/6841)] Display table/cluster replication info in the table UI
* [[6900](https://github.com/yugabyte/yugabyte-db/issues/6900)] YSQL: Imported from PostgreSQL: Avoid some table rewrites for ALTER TABLE .. SET DATA TYPE timestamp
* [[6928](https://github.com/yugabyte/yugabyte-db/issues/6928)] [[7105](https://github.com/yugabyte/yugabyte-db/issues/7105)] Add tracing support for yb-client/transactions
* [[6966](https://github.com/yugabyte/yugabyte-db/issues/6966)] YCQL: Java driver changes to allow primary key column with DECIMAL type.
* [[6996](https://github.com/yugabyte/yugabyte-db/issues/6996)] ybase: List the host for pending remote bootstrap
* [[7015](https://github.com/yugabyte/yugabyte-db/issues/7015)] Allow providing restore time for snapshot
* [[7016](https://github.com/yugabyte/yugabyte-db/issues/7016)] docdb: Enabled sanity check that tablet lookup result matches partition key
* [[7038](https://github.com/yugabyte/yugabyte-db/issues/7038)] ybase: Make callhome_enabled a runtime flag

### Bug Fixes

#### Yugabyte Platform

* [[5172](https://github.com/yugabyte/yugabyte-db/issues/5172)] Health check time is always in the Pacific timezone
* [[5489](https://github.com/yugabyte/yugabyte-db/issues/5489)] Fix slow query panel returning TypeError from API request.
* [[5489](https://github.com/yugabyte/yugabyte-db/issues/5489)] [YW] Add tab panels to Queries tab to separate live from slow queries.
* [[5571](https://github.com/yugabyte/yugabyte-db/issues/5571)] Use local YSQL socket for backups if --ysql_enable_auth is true
* [[5594](https://github.com/yugabyte/yugabyte-db/issues/5594)] Platform: Handle invalid certs/keys correctly
* [[5694](https://github.com/yugabyte/yugabyte-db/issues/5694)] Platform: Fixed Metrics custom time window issue
* [[6164](https://github.com/yugabyte/yugabyte-db/issues/6164)] Manual backup of ysql task is not showing up in the universe tasks page 
* [[6320](https://github.com/yugabyte/yugabyte-db/issues/6320)] Platform: Create Universe does not validate encryption-at-rest
* [[6583](https://github.com/yugabyte/yugabyte-db/issues/6583)] Platform: Fixed the UI alignment for on-prem universes
* [[6758](https://github.com/yugabyte/yugabyte-db/issues/6758)] For on-prem universes, unable to reuse certain instances that are disassociated from a universe
* [[6759](https://github.com/yugabyte/yugabyte-db/issues/6759)] [[6760](https://github.com/yugabyte/yugabyte-db/issues/6760)] [[6814](https://github.com/yugabyte/yugabyte-db/issues/6814)] Platform: Fixed availability zone error scenarios
* [[6817](https://github.com/yugabyte/yugabyte-db/issues/6817)] [[6904](https://github.com/yugabyte/yugabyte-db/issues/6904)] Restore of large number of keyspaces/tables fails with '413 Request Entity Too Large
* [[6901](https://github.com/yugabyte/yugabyte-db/issues/6901)] Platform: YW Fails to send alert email
* [[6911](https://github.com/yugabyte/yugabyte-db/issues/6911)] Platform: Alert for removed universe fires forever
* [[6919](https://github.com/yugabyte/yugabyte-db/issues/6919)] Fix crashing on-prem provider page due to missing instance type details
* [[6965](https://github.com/yugabyte/yugabyte-db/issues/6965)] YSQL backups with node-to-node TLS encryption enabled hang forever
* [[6983](https://github.com/yugabyte/yugabyte-db/issues/6983)] Increase ssh timeout for checking instance availability
* [[7082](https://github.com/yugabyte/yugabyte-db/issues/7082)] Platform: Health-check script generates an alert for 2.5.x when using Python 3
* [[7107](https://github.com/yugabyte/yugabyte-db/issues/7107)] Platform: Changing node filter for universe metrics does not update the graphs
* [[7114](https://github.com/yugabyte/yugabyte-db/issues/7114)] Platform: Fixes a bug while backing up multiple ysql namespaces for encrypted at rest universe
* [[7159](https://github.com/yugabyte/yugabyte-db/issues/7159)] Allow only one cluster per K8s provider with namespace
* [[7196](https://github.com/yugabyte/yugabyte-db/issues/7196)] Health checks should default to TLSv1.2
* [[7233](https://github.com/yugabyte/yugabyte-db/issues/7233)] Fix data format for slow query top sql statement data cells
* [[7268](https://github.com/yugabyte/yugabyte-db/issues/7268)] Use the correct SSH user for on-prem node preflight checks

#### Core Database

* [[4599](https://github.com/yugabyte/yugabyte-db/issues/4599)] YSQL: Fix calculation for transaction counts in YSQL metrics.
* [[4813](https://github.com/yugabyte/yugabyte-db/issues/4813)] YSQL: Imported from PostgreSQL: Don't log incomplete startup packet if it's empty
* [[5007](https://github.com/yugabyte/yugabyte-db/issues/5007)] YSQL: Properly set retain_delete_markers for colocated tables
* [[6397](https://github.com/yugabyte/yugabyte-db/issues/6397)] ybase: blacklisted TS' initial load not replicated during master failover
* [[6406](https://github.com/yugabyte/yugabyte-db/issues/6406)] docdb: Fixed SPLIT_OP replay during bootstrap of the tablet that was crashed before SPLIT_OP apply
* [[6436](https://github.com/yugabyte/yugabyte-db/issues/6436)] YSQL: Avoid cache invalidation initiated by other process
* [[6537](https://github.com/yugabyte/yugabyte-db/issues/6537)] YSQL: Backup: Fixed CREATE INDEX statement generated by ysql_dump to use SPLIT INTO syntax.
* [[6593](https://github.com/yugabyte/yugabyte-db/issues/6593)] Additional default firewall ports
* [[6628](https://github.com/yugabyte/yugabyte-db/issues/6628)] docdb: Read Table Partitions from Snapshot
* [[6735](https://github.com/yugabyte/yugabyte-db/issues/6735)] YCQL: Treat overwritten docdb collection entries as expired
* [[6811](https://github.com/yugabyte/yugabyte-db/issues/6811)] YSQL: apply empty deletes for index backfill
* [[6829](https://github.com/yugabyte/yugabyte-db/issues/6829)] [[6879](https://github.com/yugabyte/yugabyte-db/issues/6879)] YCQL: Fixed various issues for literals of collection datatypes
* [[6876](https://github.com/yugabyte/yugabyte-db/issues/6876)] 2dc: Fix data race between TwoDCOutputClient::WriteCDCRecordDone and TwoDCOutputClient destructor
* [[6881](https://github.com/yugabyte/yugabyte-db/issues/6881)] YSQL: Imported from PostgreSQL: Fix calculation of how much shared memory is required to store a TOC
* [[6886](https://github.com/yugabyte/yugabyte-db/issues/6886)] YSQL: Backup: Fixed backup-restore failure due to unexpected NULLABLE column attribute value.
* [[6890](https://github.com/yugabyte/yugabyte-db/issues/6890)] docdb: Fixed tablet splitting partitions version refresh
* [[6902](https://github.com/yugabyte/yugabyte-db/issues/6902)] Remove recently added LongOperationTracker from ThreadPool
* [[6903](https://github.com/yugabyte/yugabyte-db/issues/6903)] YSQL: Imported from PostgreSQL: Fix pg_dump for GRANT OPTION among initial privileges.
* [[6943](https://github.com/yugabyte/yugabyte-db/issues/6943)] YSQL: Imported from PostgreSQL: Fix ALTER DEFAULT PRIVILEGES with duplicated objects
* [[6953](https://github.com/yugabyte/yugabyte-db/issues/6953)] docdb: Intent metadata_write_time is incorrectly set
* [[6960](https://github.com/yugabyte/yugabyte-db/issues/6960)] docdb - Register ScopedRWOperation when accessing doc_db data in StillHasParentDataAfterSplit
* [[7008](https://github.com/yugabyte/yugabyte-db/issues/7008)] YQL: PermissionsManager::AlterRole() should always rebuild permissions
* [[7040](https://github.com/yugabyte/yugabyte-db/issues/7040)] 2DC: Fix Race Condition On Consumer With Smaller Batch Sizes
* [[7070](https://github.com/yugabyte/yugabyte-db/issues/7070)] YSQL: Imported from PostgreSQL: 'Fix ancient memory leak in contrib/auto_explain.'
* [[7071](https://github.com/yugabyte/yugabyte-db/issues/7071)] Update to latest version of cqlsh
* [[7079](https://github.com/yugabyte/yugabyte-db/issues/7079)] [[7153](https://github.com/yugabyte/yugabyte-db/issues/7153)] YSQL: Improve configuration and defaults for client to server encryption
* [[7094](https://github.com/yugabyte/yugabyte-db/issues/7094)] YSQL: Imported from PostgreSQL: 'Avoid crash when rolling back within a prepared statement.'
* [[7097](https://github.com/yugabyte/yugabyte-db/issues/7097)] YSQL: Allow table size hints to be used by query planner
* [[7105](https://github.com/yugabyte/yugabyte-db/issues/7105)] Explicitly clear the global PgMemctx map in YBCDestroyPgGate
* [[7109](https://github.com/yugabyte/yugabyte-db/issues/7109)] Fix a race in accessing the YQLPartitionsVTable::cache_ object
* [[7151](https://github.com/yugabyte/yugabyte-db/issues/7151)] YSQL: disallow tampering with postgres role
* [[7192](https://github.com/yugabyte/yugabyte-db/issues/7192)] YSQL: Imported from PostgreSQL: 'Fix usage of whole-row variables in WCO and RLS policy expressions.'
* [[7237](https://github.com/yugabyte/yugabyte-db/issues/7237)] YSQL: Imported from PostgreSQL: 'pg_attribute_no_sanitize_alignment() macro'
* [[7241](https://github.com/yugabyte/yugabyte-db/issues/7241)] YSQL: Imported from PostgreSQL: 'Avoid divide-by-zero in regex_selectivity() with long fixed prefix.'

## v2.5.1 - Jan 14, 2021

**Build:** `2.5.1.0-b153`

### Downloads

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.5.1.0-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.5.1.0-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.5.1.0-b153
```

### New features

#### Yugabyte Platform

* [#5723] Support for custom CA signed certificates for encryption in-flight 
* [#5556] Add alerts capability for backup tasks

#### Core Database

* [#6010] YCQL: Cache authentication information
* [#4874] YSQL: Backup for colocated databases
* [#4899] YSQL: Index backfill unique indexes 
* [#6237] YSQL: add CREATE INDEX NONCONCURRENTLY grammar 
* [#5982] YSQL: Add support for CREATE TABLE/INDEX ... WITH (table_oid = x) 
* [#4770] YSQL: Completing feature RANGE Partitioning
* Improved Clock Skew Handling
    * [#6370] Bump default max_clock_skew to 500ms
    * [#3335] Crash when too big clock skew is detected

### Improvements

#### Yugabyte Platform

* [#5888] Better error for handling when user creates concurrent backup tasks 
* [#6016] Pre-flight checks for create universe / edit universe / add node operations
* [#4183] Add error message for invalid ssh keys for onprem providers
* [#5848] Onprem provider creation failure leaves incomplete state behind
* [#5652] Improve delete universe handling for failed universes
* [#6254] Allow user to input multiple GFlag entries at the same time
* [#4843] New Health check for Replication status in 2dc setup
* [#6268] Utility script to edit universe json from command line
* [#4795] Embed http proxy within yb platform to avoid need for deploying nginx
* [#5799] Delete platform code related to dev/debug package installs and EPEL repo
* [#6228] Enabling Encryption-at-Rest without KMS config causes Create Universe to fail silently
* [#6289] Fixed degraded performance on Live Queries tab caused by huge number of DOM nodes in rows
* [#6383] Disabled unused API endpoint: run_query 
* [#6384] Disabled unused API endpoint: run_in_shell 
* [#6389] Input validation for kubernetes config path 
* [#6386] Added input validation for access_keys API endpoint 
* [#6382] Added UI validation for backup target paths
* #6175: Use sudo when removing prometheus snapshots during platform backup
* [#6602] Fix TLS directory while provisioning YB nodes.
* [#6683] Use a timeout mechanism in cluster_health.py that is more compatible with docker's lack of zombie reaping
* [#6633] Platform: rename Pivotal to VMware Tanzu, add Red Hat OpenShift cloud provider

#### Core Database

* [#6580] ycql: Log the status when statement fails for system query cache
* [#6608, #6609] YCQL: Consolidate authentication error handling and messages
* [YCQL][#6374] Upgrade spark-cassandra-connector version to 2.4-yb-3
* #5678: [YCQL] Optimize updating non-indexed columns within indexed tables.
* [#3329][YSQL] Optimized updates to reflect changes on necessary indexes only
* [#5805] [YSQL] Use slots more widely in tuple mapping code and make naming more consistent
* [YSQL] Clean up libpq connection code (#6481)
* [#6417] [YSQL] Backport 'Skip allocating hash table in EXPLAIN-only mode'
* [#6131] [YSQL] Dowgrade permission check for data directory to warning
* [YSQL] Bubble up backfill error message (#6292)
* [#5805] [YSQL]  Don't require return slots for nodes without projection
* [#5805] [YSQL] Split ExecStoreTuple into ExecStoreHeapTuple and ExecStoreBufferHeapTuple
* [#5805] [YSQL] Error position support for defaults and check constraints
    * Improvements to 2DC x-cluster async replication 
    * [#6169] Correctly replay write batches with external intents on bootstrap
    * [#6169] New format for external intents
    * [#6283] Fix Very Large Metric Lag for 2DC Txns
    * [#3522] Fix Threading Issues with CDC Consumer Writes
    * [#6068] Replicate Intents and Apply Messages for 2DC Txns
    * [#6169] Fix apply order of updated external intents records
    * [#4516] 2DC: Initial support for colocated databases
* Improvements to Tooling
    * [#6589] docdb: Add option to clear placement info in yb-admin.
    * [#6223] master UI make hash_split more readable 
    * [#6161] Change modify_table_placement_info to wipe read_replicas and affinitized_leaders
    * [#5420] ybase: Enhance YMaster admin page to display under-replicated tablets
    * [#1325] ybase: API for displaying YB version information
* Improvements to Tablet splitting
    * [#4942] docdb: tablet splitting: implemented retries to post-split involved tablets for 
    * #5937: Fixed the case when one of the tablet replicas is down during the split.
    * #6101 Add the flag to limit number of tablets per table
    * [#6424] Fix post-split compaction to be async
* Improvements to core product security
    * [#6568] Add flag to force client certificate verification in SSL
    * [#6266] Replace retry counter with check that data is ready
    * [#6266] Retry SSL_write on SSL_ERROR_WANT_WRITE
    * [#6266] Fix handling SSL_write error
* [#6394, #6434] docdb: Speedup system.partitions queries
* [#3979] Add Transaction Cleanup to Catalog Manager Create DDLs
* [#1258]: Send election request from master during table creation
* [#6114] docdb: Add metrics for master YCQL system table
* [#1259] Speedup DROP TABLE
* [#5752] Avoid starving threadpool with run election tasks from FailureDetector
* [#5755] Faster cleanup of transactions that failed to commit due to a concurrent abort
* (#5996) Messenger::ScheduleOnReactor should break loop finding reactor
* [#6305] Adaptive Heartbeat Reporting
* [#6445] docdb: Master should rebuild YCQL system.partitions on a background thread
* [#6696] Small master perf tweaks

### Bug Fixes

#### Yugabyte Platform

* [#6300] Install of s3cmd fails for default GCP OS image when airgapped
* [#6085] Fixed the issue where master is brought up in read replica cluster
* [#6416] Deletion with flag --node_ip fails for onprem universes.
* [#6614] Fixed NPE with full move
* [#6252] Fixed NPE on the metrics page
* [#5942] Fixed an issue where release instance is not an option for a node should the install fail because of ssh access
* [#6275] Fixed missing stats on Nodes page when universe has read replicas
* [#6257] Updating user profile when smtp username or password are empty

#### Core Database

* [#6144] YCQL: Fix handling of tablet-lookup errors in Executor::FlushAsync
* [#6570] YSQL: Use IsYBRelation instead of IsYugaByteEnabled
* [#6492] YSQL Avoid memcpy() with a NULL source pointer and count == 0
* [YSQL] Fix IsCreateTableDone for index backfill (#6234)
* [#6318, #6334] Call InitThreading in YSQL webserver process
* #6317: [YSQL] Fixed SIGSERV in YBPreloadRelCache
* [#6364] [YSQL] Replace CurrentMemoryContext with GetCurrentMemoryContext for all pg_extensions
* #6219] [YSQL] PRIMARY KEY index in TEMP TABLE is not checked for uniqueness
* (#6284) [YSQL] Clear ALTERING when there's no alter 
* [#5805] [YSQL] Fix run-time partition pruning for appends with multiple source rels
* [#6151] [YSQL] Handle rowmark in case of read with batch of ybctids
* (#6270) [YSQL] Prevent concurrent backfill index
* [#6133] [YSQL] Fix procedure with an INOUT parameter in DO block
* [#6061] [YSQL] ysql_dump should consistently use quotes on column names
* #6009: [backup][YSQL] Fixed incorrect column-ids in restored YSQL table if original table was altered.
* #5954 [YSQL] Check transaction status after read finished
* #6430 [YSQL] Refresh YBCache in case postgres clears its internal cache
* #6468 [YSQL] Fix read restarts of request with paging state
* [#6435] docdb: fixed handling of empty bloom filter key in the write path
* [#6435] Fixed bloom filter index generation for range-partitioned tablets
* [#6375] Check against TabletPeer returning null tablet pointer in 
* (#5641) Fix for --cert_node_filename for tservers and --enable_ysql
* [#6318, #6334] Call InitThreading in YSQL webserver process
* [#4150] Fix incorrect tracking of flushed/synced op id in case of Raft operation abort
* (#6278) Master SEGV during LB due to null TSDescriptor 
* [#6353] Disable rocksdb flush on all DeleteTablet calls
* [#6338] Fix crash with redis workloads and snapshot restore
* [#6334] Attach/detach Squeasel threads to/from libcds using callbacks
* [#6245] Fixed incorrect restored table schema if the table was altered after the backup.
* [#6217] Avoid a possible assertion failure in CDSAttacher destructor in Webserver
* [#6170] Shutdown status resolver before destroying it in ResolveIntents
* [#6482] Fix timeout handling when getting safe time in a RF1 cluster
* [#6635] Fix the wrong detection of communication failure when no operations transferred
* [#6678] backup: Fix restore of colocated table with table_oid already set

### Known Issues

#### Yugabyte Platform

* Azure IaaS orchestration
    * No pricing information provided (5624)
    * No support for regions with zero Availability Zones(AZs) (5628)

#### Core Database

* Advisory on clock-skew
    * After commit `a60a4ae00d217563cac865b3363e2c2bb8aa58ba`, by default, any YB node will explicitly crash if it detects a clock skew higher than the `max_clock_skew` flag (default 500ms). This can be disabled by setting `fail_on_out_of_range_clock_skew=false`, but this could lead to consistency issues! The recommendation is that you ensure clocks are synchronized across your cluster.

## v2.5.0 - Nov 12, 2020

**Build:** `2.5.0.0-b2`

### Downloads

<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.5.0.0-darwin.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/yugabyte-2.5.0.0-linux.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.5.1.0-b153
```

### New features

#### Yugabyte Platform

* **Azure Cloud integration for Yugabyte Platform (in beta):** 

    [Yugabyte Platform is natively integrated with Azure cloud](http://blog.yugabyte.com/introducing-yugabyte-platform-on-azure-beta-release/) to simplify deploying, monitoring, and managing YugabyteDB deployments. This feature automates a number of operations including orchestration of instances, secure deployments, online software upgrades, and scheduled backups, as well as monitoring and alerting. (6094, 6020)

* Yugabyte Platform operations now allow promoting a Yugabyte TServer only node to run Yugabyte Master and TServer process (5831)

#### Core Database

* **Enhanced multi-region capabilities with geo-partitioning and follower reads**

    The YugabyteDB 2.5 release adds [row-level geo-partitioning capabilities](https://blog.yugabyte.com/geo-partitioning-of-data-in-yugabytedb/) as well as follower reads to the extensive set of multi-region features that YugabyteDB already had.

* **Enterprise-grade security features:** 

    Authentication using the highly secure SCRAM-SHA-256 is now supported to limit security risks from brute force attacks and sniffing, including LDAP support for better user management and the ability to audit all database operations. 

* **Table-level partitions** allow users to split what is logically one large table into smaller sub-tables, using the following types of table partitioning schemes that PostgreSQL supports: _range partitioning_, _list partitioning,_ and _hash partitioning_. Read more about [table partitioning in YugabyteDB](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-row-level-partitioning.md). 
* **Event triggers** are now supported in addition to regular table-level triggers in YSQL. While regular triggers are attached to a single table and capture only DML events, event triggers are global to a particular database and are capable of capturing DDL events. The event-based trigger framework enables detecting changes made to the data, and automating any subsequent tasks that need to be performed, which are useful in a number of use cases such as implementing a consolidated, central audit table (2379)
* **Simplified cluster administration:** 
    * **Online rebuild of indexes** is supported for both the YSQL and YCQL APIs. This means that new indexes can be added to tables with pre-existing data while concurrent updates are happening on the cluster. The online index rebuild  process creates the newly added index in the background, and transactionally enables the index once the rebuild of all the data is completed. This feature allows flexibility of adding indexes as the application needs evolve to keep queries efficient.
    * **Cluster execution statistics and running queries** can be analyzed in detail, allowing administrators to gain insights into how the database is performing. The <code>[pg_stat_statements](https://www.postgresql.org/docs/11/pgstatstatements.html)</code> extension, which enables tracking execution statistics of all SQL statements executed by the cluster, is supported and enabled by default.  Support for <code>pg_stat_activity</code> has also been added, which shows information related to the activity performed by each connection. Yet another useful feature in this category is the ability to view all the live queries being executed by the cluster at any point in time.
    * <strong>Detailed query plan and execution analysis </strong>can now be performed with commands such as <code>EXPLAIN</code> and <code>EXPLAIN ANALYZE</code>. These commands display the execution plan generated by the planner for a given SQL statement. The execution plan shows details for any SQL statement such as how tables will be scanned (plain sequential scan, index scan), what join algorithms will be used to fetch required rows from the different tables, etc.

### Improvements

#### Yugabyte Platform

* Enhancements to on-prem deployment workflows 
    * Do not fail universe creation if cronjobs can't be created for on-prem (5939)
    * Remove pre-provision script requirement for air-gapped installations (5929)
    * "Sudo passwordless" in on-prem cloud provider configuration toggle is renamed
    * Allow yugabyte user to belong to other user groups in Linux (5943)
    * Added a new "Advanced" section  in on-prem cloud provider configuration which includes
        * Use hostnames
        * Desired home directory
        * Node exporter settings
    * Improvements to installation of Prometheus Node Exporter utility workflow (5926)
        * Prometheus Node exporter option is now available in the cloud configuration under advanced settings
        * Supports bringing your own node exporter user
* Make YEDIS API optional for new Universes and no change in behavior of existing universes (5207)
* Yugabyte Platform now provides alerts for backup tasks (5556)
* UI/UX improvements for YB Platform
    * Add visual feedback when backup or restore is initiated from modal (5908)
    * Minor fixes to primary cluster widget text issue (5988)
    * Show pre-provision script in UI for non-passwordless sudo on-prem provider (5550)
    * Update backup target and backup pages (5917)
* For Yugabyte Universes with Replication Factor(RF) > 3, change the default min_num replicas for even distribution of AZs across all regions (5426)
* Added functionality to create IPv6 enabled Universe in Kubernetes (5309, 5235)

#### Core Database

* Support for SQL/JSON Path Language( jsonb_path_query) (5408)
* Incorrect index update if used expression result is still unchanged (5898)
* As part of the Tablet Splitting feature
    * Implemented cleanup of the tablet for which all replicas have been split for (4929)
    * Compaction improvements (5523)
* Improve performance for sequences by using higher cache value by default. Controlled by a tserver gflag `ysql_sequence_cache_minval`  (6041)
* Added compatibility mode in the yb_backup script for Yugabyte version &lt; 2.1.4 (5810)
* Stability improvements: Make exponential backoff on lagging RAFT followers send NOOP instead of reading 1 op from disk (5527)
* Added use of separate metrics objects for RegularDB and IntentsDB (5640)

### Bug Fixes

#### Yugabyte Platform

* Fix for Universe disk usage shows up empty on the universe page (5548)
* Fix on on-prem backup failures due to file owned by the root user (6062)
* Fix for a bug where user operation to perform a change to nodes count by AZ was doing a full move (5335)
* Fixes for Yugabyte Platform data backup script for Replicated based installations
* Fixes to Client Certificate start time to use UTC during download (6118)
* Fixes for migration if no access keys exist yet (6099)
* Fix to resolve issues caused by starting a Yugabyte TServer Node when another Yugabyte Master node is down in the Universe (5739)
* Use the correct disk mount point while calculating disk usage of logs (5983)
* Fixes to delete backups for TLS Enabled Universes (5980)

#### Core Database

* Fix for bug with the duplicate row detection that allows a unique index to get created when the table is not unique on the index column(s) (5811)
* Improve fault tolerance of DDLs and reduce version mismatch errors in YSQL (3979, 4360)
* Fixes to incorrect column-ids in the restored table if the original table was altered (5958)
* Fixes timeout bug in YB Platform when there are Read Replicas. This fix will ignore read replica TServers when running AreLeadersOnPreferredOnly (6081)
* Fixes to restore of YSQL Backups after dropping and recreating a database (5651)
* Fixes to 2DC(x-cluster replication) by adding TLS support for cleaning up cdc_state table (5905)

### Known Issues

#### Yugabyte Platform

* Azure IaaS orchestration -
    * No pricing information provided  (5624) 
    * No support for regions with zero Availability Zones(AZs) (5628)

## Notes

{{< note title="New release versioning" >}}

Starting with v2.2, Yugabyte release versions follow a [new release versioning convention](../../versioning). The latest release series, denoted by `MAJOR.ODD`, incrementally introduces new features and changes and is intended for development and testing only. Revision releases, denoted by `MAJOR.ODD.REVISION` versioning, can include new features and changes that might break backwards compatibility. For more information, see [Supported and planned releases](../../releases-overview).

{{< /note >}}

{{< note title="Upgrading from 1.3" >}}

Prior to v2.0, YSQL was still in beta. Upon release of v2.0, a backward-incompatible file format change was made for YSQL. For existing clusters running pre-2.0 release with YSQL enabled, you cannot upgrade to v2.0 or later. Instead, export your data from existing clusters and then import the data into a new cluster (v2.0 or later).

{{< /note >}}
