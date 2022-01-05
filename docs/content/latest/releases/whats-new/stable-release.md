---
title: What's new in the v2.8 stable release series
headerTitle: What's new in the v2.8 stable release series
linkTitle: v2.8 (stable)
description: Enhancements, changes, and resolved issues in the current stable release series recommended for production deployments.
aliases:
  - /latest/releases/whats-new/stable-releases/
menu:
  latest:
    identifier: stable-release
    parent: whats-new
    weight: 2586
isTocNested: true
showAsideToc: true
---

Included here are the release notes for all releases in the v2.8 stable release series. Content will be added as new notable features and changes are available in the patch releases of the v2.8 stable release series.

For an RSS feed of the release notes for the latest and stable releases, point your feed reader to [https://docs.yugabyte.com/latest/releases/whats-new/index.xml](../index.xml).

## v2.8.1.0 - January 4, 2022 {#v2.8.1.0}

**Build:** `2.8.1.0-b37`

### Downloads

<a class="download-binary-link" href="https://downloads.yugabyte.com/releases/2.8.1.0/yugabyte-2.8.1.0-b37-darwin-x86_64.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/releases/2.8.1.0/yugabyte-2.8.1.0-b37-linux-x86_64.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux x86</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/releases/2.8.1.0/yugabyte-2.8.1.0-b37-el8-aarch64.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux ARM</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.8.1.0-b37
```

### New features

#### Yugabyte Platform

* [PLAT-2295] [PLAT-2296] Timezone Selector on user profile
* [PLAT-2306] Add UI support for GCP user tags

#### Database

* [[7809](https://github.com/yugabyte/yugabyte-db/issues/7809)] [YSQL] Add session flag to enable non-transactional writes on COPY
* [[10094](https://github.com/yugabyte/yugabyte-db/issues/10094)] [DocDB] Implemented advanced delta encoding/decoding optimized for DocDB-specific rocksdb keys
* [[10204](https://github.com/yugabyte/yugabyte-db/issues/10204)] [YSQL] Add functionality for the yb_fdw role
* [[10473](https://github.com/yugabyte/yugabyte-db/issues/10473)] Implement YSQL Follower reads.
* [[10563](https://github.com/yugabyte/yugabyte-db/issues/10563)] [xCluster] Create Rename Replication Command

### Improvements

#### Yugabyte Platform

* [PLAT-506] Allow custom port in AWS host base
* [PLAT-2103] [UI] [Encr at rest] AWS credentials enabled when IAM is checked in
* [PLAT-2298] Allow outOfOrder migrations and ignoreMissingMigrations
* [PLAT-2420] ".local" as a TLD in AWS host base
* [PLAT-2522] Backport Migrations for LDAP support
* Add leader blacklisting to UpgradeTaskBase, Stop Processes, upgradeKubernetes, and handling rf1 universes
* Update certs to use certLabel as CN

#### Database

* [[10428](https://github.com/yugabyte/yugabyte-db/issues/10428)] [DocDB] Improve logging for SST file expiration
* [[10482](https://github.com/yugabyte/yugabyte-db/issues/10482)] [DST] Rate-limit Create/Delete Snapshot requests
* [[10503](https://github.com/yugabyte/yugabyte-db/issues/10503)] [DocDB] Add GFlag to trust value-level TTL metadata during file expiration.
* [[10543](https://github.com/yugabyte/yugabyte-db/issues/10543)] [YCQL] Allow NULL in filtering clauses WHERE/IF inside IN/NOT IN operators in SELECT statement.
* [[10579](https://github.com/yugabyte/yugabyte-db/issues/10579)] [YSQL] Import libpq: reject extraneous data after SSL or GSS encryption handshake.
* [[10655](https://github.com/yugabyte/yugabyte-db/issues/10655)] [YSQL] Add --postmaster_cgroup gflag to tserver
* [[10883](https://github.com/yugabyte/yugabyte-db/issues/10883)] [YSQL] Rename the GUC for disabling transactional writes

### Bug fixes

#### Yugabyte Platform

* [[10907](https://github.com/yugabyte/yugabyte-db/issues/10907)] [Backup] Prevent double config file uploading.
* [PLAT-1990] Ensure universe size doesn't change dramatically during a full move
* [PLAT-2080] Set Universe keys before starting tserver on encrption at rest enabled universes.
* [PLAT-2101] Save button doesn't throw field errors when all the fields are empty
* [PLAT-2102] Stop creation of KMS configs with duplicate name
* [PLAT-2298] Disable third-party flyway plugin in yugabundle
* [PLAT-2121] [UI] [KMS UI] displaying Successfully added even when it fails to add KMS config
* [PLAT-2177] Avoid adding duplicate delete backup tasks to tasks queue
* [PLAT-2246] Fix Timezone UI Bugs
* [PLAT-2311] API Should return region active status
* [PLAT-2314] [PLAT-2321] Fix backup util behavior on error for onprem providers
* [PLAT-2339] [UI] Disable toggle TLS / cert rotation dialogs in UI for k8s universes
* [PLAT-2351] Disable upgrade to systemd for manually provisioned universes
* [PLAT-2373] Get rid of repeated migrations
* [PLAT-2400] Universe scaling out by adding nodes keeps new nodes in blacklisted state.
* [PLAT-2427] [UI] No way to remove region from provider
* [PLAT-2498] Fix for Wrong timestamps in Health Check and Users Tab
* [PLAT-2517] Fix typo (missing backslash) in the priority-regex for prometheus in `replicated.yaml`

#### Database

* [[10256](https://github.com/yugabyte/yugabyte-db/issues/10256)] [PITR] Fix Incorrect Restore logic for System Catalog tables
* [[10591](https://github.com/yugabyte/yugabyte-db/issues/10591)] [YSQL] COPY for partitioned table writes to parent instead of child nodes
* [[10646](https://github.com/yugabyte/yugabyte-db/issues/10646)] [YSQL] fix client backfill timeout

### Known issues

#### Yugabyte Platform

* N/A

#### Database

* N/A

## v2.8.0.0 - November 16, 2021 {#v2.8.0.0}

**Build:** `2.8.0.0-b37`

### Downloads

<a class="download-binary-link" href="https://downloads.yugabyte.com/releases/2.8.0.0/yugabyte-2.8.0.0-b37-darwin-x86_64.tar.gz">
  <button>
    <i class="fab fa-apple"></i><span class="download-text">macOS</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/releases/2.8.0.0/yugabyte-2.8.0.0-b37-linux-x86_64.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux x86</span>
  </button>
</a>
&nbsp; &nbsp; &nbsp;
<a class="download-binary-link" href="https://downloads.yugabyte.com/releases/2.8.0.0/yugabyte-2.8.0.0-b37-el8-aarch64.tar.gz">
  <button>
    <i class="fab fa-linux"></i><span class="download-text">Linux ARM</span>
  </button>
</a>
<br />

### Docker

```sh
docker pull yugabytedb/yugabyte:2.8.0.0-b37
```

### New features

For an overview of the 2.8.0.0 release's notable new features, check out the [release announcement blog post](https://blog.yugabyte.com/announcing-yugabytedb-2-8/).

#### Yugabyte Platform

* We've made some significant updates to alerts and notifications. You can read about them in [this blog post](https://blog.yugabyte.com/yugabytedb-2-8-alerts-and-notifications/).

* The [Yugabyte Platform REST API](https://api-docs.yugabyte.com/) is now available. Be sure to read the [blog post](https://blog.yugabyte.com/yugabytedb-2-8-api-automated-operations/) about it, as well!

#### Database

* [[1127](https://github.com/yugabyte/yugabyte-db/issues/1127)] [YSQL] Collation Support
* [[7850](https://github.com/yugabyte/yugabyte-db/issues/7850)] [YSQL] create new access method ybgin

### Improvements

#### Yugabyte Platform

* [[9131](https://github.com/yugabyte/yugabyte-db/issues/9131)] [Platform] Enable/disable YCQL endpoint while universe creation and force password requirement
* [[9580](https://github.com/yugabyte/yugabyte-db/issues/9580)] [Platform] Add restore_time field for all universes.
* [[9613](https://github.com/yugabyte/yugabyte-db/issues/9613)] [Platform] Update UI to accommodate Authentication changes
* [[9733](https://github.com/yugabyte/yugabyte-db/issues/9733)] [Platform] [Alerts] Implement alert listing
* [[9978](https://github.com/yugabyte/yugabyte-db/issues/9978)] [Platform] [UI] Change stop backup icon and label to abort icon and label.
* [CLOUDGA-1880] enable JSON logging for cloud deployments
* [CLOUDGA-2345] [Platform] implement MDC propagation and add request/universe ID to MDC
* [PLAT-26] [[9612](https://github.com/yugabyte/yugabyte-db/issues/9612)] Add logs purge threshold option to zip_purge_yb_logs.sh
* [PLAT-59] [[5236](https://github.com/yugabyte/yugabyte-db/issues/5236)] Allow log levels to be changed through POST /logging_config endpoint
* [PLAT-386] [[9407](https://github.com/yugabyte/yugabyte-db/issues/9407)] Implement base YSQL/YCQL alerts
* [PLAT-417] Add support for Ubuntu 18.04 distributions
* [PLAT-490] Display timezone with timestamp
* [PLAT-523] [[7645](https://github.com/yugabyte/yugabyte-db/issues/7645)] Show error summary at the top of the health check email
* [PLAT-541] Allow configuring no destination for alert config + UI improvements
* [PLAT-1530] Made assignStaticPublicIP optional parameter for create universe. Ran swaggerGen.
* [PLAT-1556] List Storage Configs Create Scheduled backup examples
* [PLAT-1573] Adding 'create new cert' in enable TLS new feature
* [PLAT-1582] [Alert] Limit Severity to maximum 2(Severe/warn), now we can add multiple severity's but after edit we are displaying only 2 (1 Severe/1 Warn)
* [PLAT-1585] k8s example for create universe
* [PLAT-1620] Added secondary subnet for allowing two network interfaces
* [PLAT-1647] Provide more details for default channel on UI
* [PLAT-1664] Enable new alert UIs and remove deprecated alert UI + configs from Health tab + config from replication tab
* [PLAT-1669] initial OEL 8 support
* [PLAT-1691] Task, API and thread pool metrics
* [PLAT-1704] Make Platform health checks more scalable
* [PLAT-1704] WIP: Make Platform health checks more scalable
* [PLAT-1705] Add auditing and transaction for /register API action
* [PLAT-1731] Add more logging for Platform HA feature
* [PLAT-1747] supporting n2 instance types for GCP internally
* [PLAT-1766] [Alerts] [UI] Cleanup
* [PLAT-1774] Add a customer ID field in Customer Profile page
* [Plat-1777] Add basic filtering and sorting
* [PLAT-1793] DB Error logs alert
* [Plat-1797] Create a pagination component
* [PLAT-1808] [Alert UI] cleanup tasks
* [PLAT-1817] Add support for new certificate creation when rotating certs
* [PLAT-1818] Add pagination to Tables tab and add classNames
* [PLAT-1824] Improve backup retention in case of backup failure
* [PLAT-1855] Edit Universe example and missing implicit params
* [PLAT-1867] AWS Provider and Universe examples
* [PLAT-1934] Adding UI to set KUBE_DOMAIN
* [PLAT-1943] Remove feature flagging for enable/disable TLS
* [PLAT-1956] Expose on-prem node pre-flight checks as a standalone action
* [PLAT-1962] Add optional AWS KMS Endpoint field while creating KMS config.
* [PLAT-1989] Show alert configuration target in page view
* [PLAT-2033] [Alert] [UI] Move seconds in Duration under conditions similar to Threshold in Alert Definition Page
* [PLAT-2071] Implement read-only user functionality for Alert UIs
* [PLAT-2104] Enable/disable Auth in k8s
* [PLAT-2143] [UI] Add an optional field AWS KMS Endpoint while creating KMS config
* [PLAT-2229] Retrieve YB version via ssh/kubectl during health check
* [Platform] Return direct URL to Prometheus for metrics from metrics API call

#### Database

* [[2220](https://github.com/yugabyte/yugabyte-db/issues/2220)] [YSQL] Enabling relation size estimation for temporary tables in optimizer
* [[2272](https://github.com/yugabyte/yugabyte-db/issues/2272)] [YSQL] Migration framework for YSQL cluster upgrade
* [[5492](https://github.com/yugabyte/yugabyte-db/issues/5492)] yb-admin: Added error message when attempting to create snapshot of YCQL system tables
* [[6541](https://github.com/yugabyte/yugabyte-db/issues/6541)] [YSQL] Enable row-locking feature in CURSOR
* [[7612](https://github.com/yugabyte/yugabyte-db/issues/7612)] [DocDB] Allow TTL-expired SST files that are too large for compaction to be directly expired
* [[7612](https://github.com/yugabyte/yugabyte-db/issues/7612)] [DocDB] Modified compaction file filter to filter files out of order
* [[7889](https://github.com/yugabyte/yugabyte-db/issues/7889)] Reduce timeout for ysql backfill.
* [[8162](https://github.com/yugabyte/yugabyte-db/issues/8162)] YSQL Support single-request optimization for UPDATE with RETURNING clause
* [[8229](https://github.com/yugabyte/yugabyte-db/issues/8229)] [Backup] repartition table if needed on YSQL restore
* [[8242](https://github.com/yugabyte/yugabyte-db/issues/8242)] [DocDB] Update defaults for automatic tablet splitting
* [[8402](https://github.com/yugabyte/yugabyte-db/issues/8402)] [YSQL] change gin to ybgin for YB indexes
* [[8452](https://github.com/yugabyte/yugabyte-db/issues/8452)] Speed up restoring YSQL system catalog
* [[8501](https://github.com/yugabyte/yugabyte-db/issues/8501)] [DocDB] Add metric to monitor server uptime
* [[8807](https://github.com/yugabyte/yugabyte-db/issues/8807)] [YBase] Add HTTP URL param for limiting the number of tables whose metrics are displayed
* [[8979](https://github.com/yugabyte/yugabyte-db/issues/8979)] [DocDB] Improve master load balancer state presentation
* [[9279](https://github.com/yugabyte/yugabyte-db/issues/9279)] [YSQL] Enable -Wextra on pgwrapper
* [[9279](https://github.com/yugabyte/yugabyte-db/issues/9279)] [YSQL] Enable -Wextra on yql folder
* [[9370](https://github.com/yugabyte/yugabyte-db/issues/9370)] Set enable_stream_compression flag to true by default
* [[9439](https://github.com/yugabyte/yugabyte-db/issues/9439)] [YBase] Allow sst-dump to decode docdb keys and dump data in human readable format
* [[9467](https://github.com/yugabyte/yugabyte-db/issues/9467)] [YSQL] Increase scope of cases where transparent retries are performed
* [[9512](https://github.com/yugabyte/yugabyte-db/issues/9512)] Add optional bootstrap IDs parameter to AlterUniverseReplication add_tables
* [[9606](https://github.com/yugabyte/yugabyte-db/issues/9606)] [docdb] Add flag --force for command delete_tablet to set state TABLET_DATA_DELETED for tool yb-ts-cli
* [[9969](https://github.com/yugabyte/yugabyte-db/issues/9969)] [DocDB] Add a gflag for rocksdb block_restart_interval
* [[10019](https://github.com/yugabyte/yugabyte-db/issues/10019)] [DocDB] Add support for zlib compression
* [[10064](https://github.com/yugabyte/yugabyte-db/issues/10064)] [xCluster] Lag Metric Improvements
* [[10094](https://github.com/yugabyte/yugabyte-db/issues/10094)] [DocDB] added data_block_key_value_encoding_format option
* [[10141](https://github.com/yugabyte/yugabyte-db/issues/10141)] [DocDB] Remove feature gate on savepoints
* [[10150](https://github.com/yugabyte/yugabyte-db/issues/10150)] [YSQL] Add functionality for the yb_extension role
* [[10240](https://github.com/yugabyte/yugabyte-db/issues/10240)] Add IPv6 address filters to default value of net_address_filter
* [[10430](https://github.com/yugabyte/yugabyte-db/issues/10430)] [YSQL] Limit to IPv4 for sys catalog initialization
* [YSQL] Foreign Data Wrapper Support
* Added more information in logs for understanding concurrency control + downgraded two log lines to VLOG(4)
* Added new AWS regions to metadata files

### Bug fixes

#### Yugabyte Platform

* [[1525](https://github.com/yugabyte/yugabyte-db/issues/1525)] [Platform] New Universe creation gets public IP assigned even with flag = false
* [[1598](https://github.com/yugabyte/yugabyte-db/issues/1598)] [Platform] [UI] Suggested Default File Path for CA Signed Certificate and Private Key is Incorrect
* [[7396](https://github.com/yugabyte/yugabyte-db/issues/7396)] [Platform] Splitting up create/provision tasks to delete orphaned resources
* [[7738](https://github.com/yugabyte/yugabyte-db/issues/7738)] [PLAT-611] Health checks can overlap with universe update operations started after them
* [[8510](https://github.com/yugabyte/yugabyte-db/issues/8510)] [Platform] Allow the deletion of Failed Backups
* [[9571](https://github.com/yugabyte/yugabyte-db/issues/9571)] [Platform] Backup and Restore failing in k8s auth enabled environment
* [[9743](https://github.com/yugabyte/yugabyte-db/issues/9743)] [Platform] Fix universe reset config option (#9863)
* [[9850](https://github.com/yugabyte/yugabyte-db/issues/9850)] [YW] Correct the node path (#9864)
* [CLOUDGA-1893] [Platform] fix client-to-node cert path in health checks
* [PLAT-253] Fix the backupTable params while creating Table backups using APIs.
* [PLAT-253] Fix universe's backupInprogress flag to avoid multiple backup at a time due to low frequency scheduler.
* [PLAT-289] Stopped node should not allow Release action
* [PLAT-368] [[9366](https://github.com/yugabyte/yugabyte-db/issues/9366)] Disable Delete Configuration button for backups when in use.
* [PLAT-482] [[7573](https://github.com/yugabyte/yugabyte-db/issues/7573)] Health Checks should run when Backup/Restore Tasks are in progress
* [PLAT-509] [[9014](https://github.com/yugabyte/yugabyte-db/issues/9014)] Refresh Pricing data for Azure provider seems to be stuck
* [PLAT-521] [[9315](https://github.com/yugabyte/yugabyte-db/issues/9315)] BackupsController: small fixes required
* [PLAT-525] Add IP address to SAN of node certificates
* [PLAT-599] Fix error messages in alert destination and configuration services
* [PLAT-1511] Fix legend overflowing in metrics tab
* [PLAT-1520] Stop displaying external script schedule among Backup Schedules.
* [PLAT-1522] Fix s3 release breakage
* [PLAT-1523] Make Alert APIs to be consistent with UI terminology
* [PLAT-1528] Change YWError handler to default to json response on client error.
* [PLAT-1530] [[9794](https://github.com/yugabyte/yugabyte-db/issues/9794)] Creates static IP during cluster creation for cloud free tier clusters. Releases IPs on deletion.
* [PLAT-1549] [PLAT-1697] Fix Stop backup race condition. Add non-schedlued backup examples
* [PLAT-1559] Stop the external script scheduler if the universe is not present.
* [PLAT-1563] Fix instance down alerts + make sure instance restart alert is not fired on universe operations
* [PLAT-1578] Do not specify storage class (use default if provided)
* [PLAT-580] Fix DB migration ordering; use repeatable for backport
* [PLAT-1586] [Alert] Able to add multiple alert configuration with same name. Add duplicate check for alert configuration name
* [PLAT-1599] [UI] Root Certificate and node-node and client-node TLS missing on Edit Universe
* [PLAT-1600] add conf entries for various ansible settings
* [PLAT-1603] YBFormInput's OnBlur throws error on AddCertificateForm
* [PLAT-1605] Fix duplicate alert definitions handling + all locks to avoid duplicates creation
* [PLAT-1606] Disk name too long for Google Cloud clone disk
* [PLAT-1607] Upgrade systemd API fix
* [PLAT-1611] Add python depedencies required for executing external scripts
* [PLAT-1613] [Alerts] Logs filled with NPE related to "Error while sending notification for alert "
* [PLAT-1617] Added GCP region metadata for missing regions.
* [PLAT-1617] Fix issue with GCP Subnet CIDR
* [PLAT-1619] Check for FAILED status in wait_for_snapshot method.
* [PLAT-1621] Health check failed in K8s portal
* [PLAT-1625] Fix task details NPE
* [PLAT-1626] Skip preprovision for systemd upgrade.
* [PLAT-1631] [Alert] Universe filter is not working in Alert Listing
* [PLAT-1634] Backup page is not loading because of empty config column
* [PLAT-1638] Fix naming convention for external script endpoints as per our standards
* [PLAT-1639] [PLAT-1681] Make proxy requests async to keep them from blocking other requests. Reduce log spew from akka-http-core for proxy requests.
* [PLAT-1644] Fix k8s universe creation failure for Platform configured with HA
* [PLAT-1646] Remove Unsupported Instance types from pull down menu for Azure
* [PLAT-1650] Added yum lock_timeout to prevent yum lockfile errors for use_custom_ssh_port.yml
* [PLAT-1653] Fix region get/list.
* [PLAT-1656] [UI] [Alert] Group Type filter is not working in Alert Listing
* [PLAT-1661] Fix alert messages for notification failures
* [PLAT-1664] Clean unused code
* [PLAT-1667] Platform should not scrape all per-table metrics from db hosts (part 2)
* [PLAT-1668] Yugabundle failing because can't find YWErrorHandler
* [PLAT-1682] Fix node comparison function from accessing undefined cluster
* [PLAT-1687] [Alert] Not able to create destination channel using "default recipients + default smtp settings + empty email field"
* [PLAT-1691] Set oshi LinuxFileSystem log level to ERROR
* [PLAT-1694] Fix Intermittent failure to back up k8s universe
* [PLAT-1707] Fix performance issue
* [PLAT-1715] Check for YB version only for 2.6+ release DB
* [PLAT-1717] Full move fails midway if system tablet takes more than 2 mins to bootstrap
* [PLAT-1721] Stop storage type from automatically changing when instance type is changed
* [PLAT-1723] Allow disabling prometheus management + write alerts and metrics effectively
* [PLAT-1726] Allow user to completely remove all gFlags after addtion of several gFlags.
* [PLAT-1730] Fix resize node logic for RF1 clusters
* [PLAT-1736] Create default alert configs and destination on DB seed
* [PLAT-1737] "This field is required" error message is shown on alert configuration creation with default threshold == 0
* [PLAT-1740] [PLAT-1886] Make backup util python3 compatible for different OS.
* [PLAT-1746] Delete prometheus_snapshot directory once Platform backup package is created
* [PLAT-1751] [UI] DB Version field setting getting reset to first item in the dropdown on toggling between the Read Replica and Primary cluster tabs
* [PLAT-1753] Enable taking backups using custom ports
* [PLAT-1757] Health Check failure message has Actual and expected values interchanged
* [PLAT-1760] Add readable type names
* [PLAT-1761] Fix alert message in case of unprovisioned nodes
* [PLAT-1768] Universe tasks take lot more time because thread pool executors do not reach max_threads
* [PLAT-1780] Redact YSQL/YCQL passwords from task_info table.
* [PLAT-1791] Use hibernate validator for all alert related entities
* [PLAT-1796] Edit Universe page has password fields editable
* [PLAT-1802] Replication graphs stopped showing on replication tab (replicated.yml change)
* [PLAT-1803] Not able to change cert for client to node in tls enable feature
* [PLAT-1804] Fix 'Querying for {} metric keys - may affect performance' log
* [PLAT-1806] Resolve issue in TlsToggle where certs_for_client_dir is set as empty
* [PLAT-1816] Forward port restricted user creation to master
* [PLAT-1819] [PLAT-1828] Release backup lock when Platform restarts, and update Backup state
* [PLAT-1831] Fix DB version dropdown from being reset when switching between primary and async cluster forms
* [PLAT-1831] Fix when navigating from home page to Create Universe
* [PLAT-1833] Fix missing create time on alert configuration creation issue
* [PLAT-1837] Change Replication factor field to be editable for async universe form.
* [PLAT-1840] Fix 30 sec customer_controller list API
* [PLAT-1842] Fix long universe list query
* [PLAT-1853] Frequent error log related to health checks on portal.k8s
* [PLAT-1862] Backup Frequency cannot be negative number
* [PLAT-1887] fix creation readonly onprem universe + code cleanup
* [PLAT-1891] [Backup] [IAM-Platform] Backup is hanging for universe with read replicas on IAM-enabled platform
* [PLAT-1892] Remove default template for error log + remove error logs from health check report
* [PLAT-1895] Fix backup failure alert in case restore fails
* [PLAT-1897] [PLAT-1995] Make client_max_body_size configurable in replicated
* [PLAT-1897] Make client_max_body_size configurable in replicated
* [PLAT-1897] Take-2. Make client_max_body_size configurable in replicated
* [PLAT-1921] [Backup] [UI] Disappearance of Encrypt backup toggle
* [PLAT-1942] Backup/restore failing on KMS enabled universes
* [PLAT-1969] [UI] Universe creation - Create button is disabled when YSQL/YCQL auth is disabled
* [PLAT-1976] Fix EditUniverse for on-prem
* [PLAT-1998] Fix NPE in SystemdUpgrade task for TLS enabled universes
* [PLAT-2002] Fixing zip_purgs_yb_logs to not error without threshold flag
* [PLAT-2012] Update cert directories gflags during cert rotation
* [PLAT-2015] Remove Sort functionality from "Target universe" in alert listing.
* [PLAT-2019] Fix permission denied issues during find command
* [PLAT-2030] [UI] UI should display the name of the newly created cert instead of "Create new cert" option
* [PLAT-2032] Append number to self-signed certificate labels when rotating certs
* [PLAT-2034] Specific task type name for tls toggle
* [PLAT-2053] Fix the wrong error message in TLS configuration modal
* [PLAT-2068] [UI] Screen going blank when removed regions in Edit Universe
* [PLAT-2069] Hiding systemd upgrade option for ReadOnly users
* [PLAT-2073] [UI] Enable Systemd Services toggle shows wrong status
* [PLAT-2081] Show Error message when trying to create existing user
* [PLAT-2092] Fix Task list default sorting by create time
* [PLAT-2094] Fix k8s universe certificate expiry checks
* [PLAT-2096] [UI] Restore backup UI refresh issue
* [PLAT-2097] Fix repeated migration V68 : approach 2
* [PLAT-2098] Certificate details page shows 'invalid date' for certificate start and expiration fields on Safari Browser only.
* [PLAT-2107] Resolve multiple UI fixes in Encryption-at-Rest modal
* [PLAT-2109] Skip hostname validation in certificate
* [PLAT-2110] Fix wrong default destination migration for multitenant Platforms.
* [PLAT-2111] Systemd upgrade failing with read replica
* [PLAT-2113] [PLAT-2117] Fix HA failing with entity_too_large
* [PLAT-2124] [Alert] [UI] Select Alert Metrics doesn't load the template if the metrics is created twice
* [PLAT-2126] Fix stopping periodical tasks in case of failure
* [PLAT-2128] Fix alert message field to print the whole message + alert channel error message fix
* [PLAT-2129] [Alert] Full Alert message is not displayed in Alert listing page on selecting the alert
* [PLAT-2134] Fix beforeValidate migration for the case of empty database
* [PLAT-2157] Flyway plugin patch for ignoreMissingMigration and default java package issue
* [PLAT-2167] Fix 3000 seconds timeout for IAM profile retrieval operation
* [PLAT-2180] [PLAT-2182] Missing error response logging when demoteInstance fails
* [PLAT-2189] Fix universe creation on airgap install
* [PLAT-2200] [UI] Fix regression with HA "standby" overlay
* [PLAT-2263] application.log not updating
* [Platform] Fix NPE in VM image upgrade for TLS enabled universes
* [Platform] Hooking GCP Create Method into Create Root Volumes method

#### Database

* [[2272](https://github.com/yugabyte/yugabyte-db/issues/2272)] [YSQL] Fix OID generation for initdb migration
* [[4421](https://github.com/yugabyte/yugabyte-db/issues/4421)] [YCQL] Disallow Unauthenticated LDAP binding + add handling for ycql_ldap_search_filter
* [[5920](https://github.com/yugabyte/yugabyte-db/issues/5920)] Fix bootstrapping with preallocated log segment
* [[7528](https://github.com/yugabyte/yugabyte-db/issues/7528)] [YSQL] Error out when Tablespaces are set for colocated tables
* [[8043](https://github.com/yugabyte/yugabyte-db/issues/8043)] [YBase] Remove information about LB skipping deleted tables from the admin UI
* [[8580](https://github.com/yugabyte/yugabyte-db/issues/8580)] [[9489](https://github.com/yugabyte/yugabyte-db/issues/9489)] [YSQL] Inherit default PGSQL proxy bind address from rpc bind address
* [[8675](https://github.com/yugabyte/yugabyte-db/issues/8675)] [DocDB] Prevent tablet splitting when there is post split data
* [[8772](https://github.com/yugabyte/yugabyte-db/issues/8772)] Fix fatal that occurs when running alter_universe_replication and producer master has
* [[8804](https://github.com/yugabyte/yugabyte-db/issues/8804)] [YSQL] [backup] Support in backups the same table name across different schemas.
* [[8807](https://github.com/yugabyte/yugabyte-db/issues/8807)] [YBase] Rename the flag controlling maxmimum number of tables to retrieve metrics for
* [[9061](https://github.com/yugabyte/yugabyte-db/issues/9061)] [docdb] Master task tracking should point to the table it is operating on
* [[9436](https://github.com/yugabyte/yugabyte-db/issues/9436)] [YSQL] Statement reads rows it has inserted
* [[9475](https://github.com/yugabyte/yugabyte-db/issues/9475)] Fetch Universe Key From Masters on TS Init
* [[9541](https://github.com/yugabyte/yugabyte-db/issues/9541)] [YSQL] Restart metrics webserver when postmaster recovers
* [[9616](https://github.com/yugabyte/yugabyte-db/issues/9616)] Fix master crash when restoring snapshot schedule with deleted namespace
* [[9655](https://github.com/yugabyte/yugabyte-db/issues/9655)] [xCluster] Label cdc streams with relevant metadata
* [[9668](https://github.com/yugabyte/yugabyte-db/issues/9668)] Alert configurations implement missing parts and few small changes
* [[9685](https://github.com/yugabyte/yugabyte-db/issues/9685)] [xCluster] Make delete_universe_replication fault tolerant
* [[9746](https://github.com/yugabyte/yugabyte-db/issues/9746)] Set WAL footer close_timestamp_micros on Bootstrap
* [[9749](https://github.com/yugabyte/yugabyte-db/issues/9749)] [DocDB] Log::CopyTo - fixed handling kLogInitialized state
* [[9762](https://github.com/yugabyte/yugabyte-db/issues/9762)] [Part-1] Populate partial index predicate in "options" column of system_schema.indexes
* [[9781](https://github.com/yugabyte/yugabyte-db/issues/9781)] Mark snapshot as deleted if tablet was removed
* [[9782](https://github.com/yugabyte/yugabyte-db/issues/9782)] docdb Tablet Splitting - Wait for all peers to finish compacting during throttling
* [[9786](https://github.com/yugabyte/yugabyte-db/issues/9786)] Universe Actions-> Add Read Replica is failing on 2.6.1.0-b23
* [[9789](https://github.com/yugabyte/yugabyte-db/issues/9789)] [docdb] Load Balancer should use tablet count while looking tablets to move
* [[9802](https://github.com/yugabyte/yugabyte-db/issues/9802)] [xCluster] Set proper deadline for YBSession in CDCServiceImpl
* [[9803](https://github.com/yugabyte/yugabyte-db/issues/9803)] [YSQL] Import Avoid trying to lock OLD/NEW in a rule with FOR UPDATE.
* [[9806](https://github.com/yugabyte/yugabyte-db/issues/9806)] [DocDB] fixed Batcher::FlushBuffersIsReady
* [[9812](https://github.com/yugabyte/yugabyte-db/issues/9812)] [YSQL] Check database is colocated before adding colocated option for Alter Table
* [[9822](https://github.com/yugabyte/yugabyte-db/issues/9822)] [DocDB] Check table pointer is not nullptr before dereferencing
* [[9831](https://github.com/yugabyte/yugabyte-db/issues/9831)] [YSQL] Import Fix broken snapshot handling in parallel workers.
* [[9855](https://github.com/yugabyte/yugabyte-db/issues/9855)] [DocDB] Set aborted subtransaction data on local apply
* [[9860](https://github.com/yugabyte/yugabyte-db/issues/9860)] [YSQL] fix limit vars to uint64
* [[9862](https://github.com/yugabyte/yugabyte-db/issues/9862)] Allow PITR in conjunction with tablet split
* [[9862](https://github.com/yugabyte/yugabyte-db/issues/9862)] PITR: Allow consecutive restore
* [[9865](https://github.com/yugabyte/yugabyte-db/issues/9865)] Fix internal retry of kReadRestart for SELECT func() with a DML in the func
* [[9867](https://github.com/yugabyte/yugabyte-db/issues/9867)] [YSQL] Fix double type overflow in case of SET yb_transaction_priority_lower_bound/yb_transaction_priority_upperr_bound command
* [[9878](https://github.com/yugabyte/yugabyte-db/issues/9878)] [YBase] Reduce regex expression evaluation in nested loop
* [[9892](https://github.com/yugabyte/yugabyte-db/issues/9892)] Mask sensitive gflag info
* [[9898](https://github.com/yugabyte/yugabyte-db/issues/9898)] [DocDB] Fix queries on system.partitions when unable to resolve some addresses
* [[9899](https://github.com/yugabyte/yugabyte-db/issues/9899)] [YSQL] Import Fix corner-case uninitialized-variable issues in plpgsql.
* [[9906](https://github.com/yugabyte/yugabyte-db/issues/9906)] [YSQL] Fix not being able to add a range primary key
* [[9909](https://github.com/yugabyte/yugabyte-db/issues/9909)] [YSQL] further fix backup restore for NULL col attr
* [[9911](https://github.com/yugabyte/yugabyte-db/issues/9911)] [YSQL] Import In pg_dump, avoid doing per-table queries for RLS policies.
* [[9922](https://github.com/yugabyte/yugabyte-db/issues/9922)] [YSQL] Import Fix float4/float8 hash functions to produce uniform results for NaNs.
* [[9924](https://github.com/yugabyte/yugabyte-db/issues/9924)] [YSQL] always check schema name on backup import
* [[9926](https://github.com/yugabyte/yugabyte-db/issues/9926)] [YSQL] Import Disallow creating an ICU collation if the DB encoding won't support it.
* [[9927](https://github.com/yugabyte/yugabyte-db/issues/9927)] YCQL - Handle unset correctly
* [[9932](https://github.com/yugabyte/yugabyte-db/issues/9932)] [YSQL] Initialize t_ybctid field in acquire_sample_rows()
* [[9933](https://github.com/yugabyte/yugabyte-db/issues/9933)] [Part-0] Update logic for using num_tablets from internal or user requests.
* [[9933](https://github.com/yugabyte/yugabyte-db/issues/9933)] [YCQL] [Part-1] DESC TABLE does not directly match the "CREATE TABLE" command for number of tablets.
* [[9934](https://github.com/yugabyte/yugabyte-db/issues/9934)] [docdb] Don't update rocksdb_dir on Remote Bootstrap
* [[9935](https://github.com/yugabyte/yugabyte-db/issues/9935)] [YSQL] Import Fix bitmap AND/OR scans on the inside of a nestloop partition-wise join.
* [[9936](https://github.com/yugabyte/yugabyte-db/issues/9936)] Alter and Create table via PgClient
* [[9936](https://github.com/yugabyte/yugabyte-db/issues/9936)] Fix ysql_dump in encrypted k8s environment
* [[9936](https://github.com/yugabyte/yugabyte-db/issues/9936)] Fix ysql_dump in TLS encrypted environment
* [[9936](https://github.com/yugabyte/yugabyte-db/issues/9936)] Generate session ID in tserver
* [[9936](https://github.com/yugabyte/yugabyte-db/issues/9936)] Remove all direct YBClient usage from PgSession
* [[9940](https://github.com/yugabyte/yugabyte-db/issues/9940)] [DocDB] use correct kv_store_id for post-split tablets
* [[9947](https://github.com/yugabyte/yugabyte-db/issues/9947)] [YSQL] remove runtime tag for ysql_disable_index_backfill
* [[9957](https://github.com/yugabyte/yugabyte-db/issues/9957)] [YSQL] Fix memory usage when translating decimal data into Postgres's datum format.
* [[9963](https://github.com/yugabyte/yugabyte-db/issues/9963)] [Backup] fix to reallow YEDIS on restore
* [[9965](https://github.com/yugabyte/yugabyte-db/issues/9965)] [YSQL] Fix copy/paste error causing incorrect conversion
* [[9966](https://github.com/yugabyte/yugabyte-db/issues/9966)] [YSQL] Import Rearrange pgstat_bestart() to avoid failures within its critical section.
* [[9969](https://github.com/yugabyte/yugabyte-db/issues/9969)] [DocDB] Couple of minor fixes
* [[9981](https://github.com/yugabyte/yugabyte-db/issues/9981)] Fix transaction coordinator returning wrong status hybrid time
* [[9994](https://github.com/yugabyte/yugabyte-db/issues/9994)] [YSQL] copy t_ybctid field in modify tuple functions
* [[9995](https://github.com/yugabyte/yugabyte-db/issues/9995)] [YSQL] Import Fix EXIT out of outermost block in plpgsql.
* [[10025](https://github.com/yugabyte/yugabyte-db/issues/10025)] [YSQL] Import jit: Do not try to shut down LLVM state in case of LLVM triggered errors.
* [[10034](https://github.com/yugabyte/yugabyte-db/issues/10034)] [YSQL] Preserve operation buffering state in case of transparent retries
* [[10038](https://github.com/yugabyte/yugabyte-db/issues/10038)] [YQL] Support for displaying the bind values for a prepared statement(s).
* [[10042](https://github.com/yugabyte/yugabyte-db/issues/10042)] [Backup] allow system table for YEDIS restore
* [[10044](https://github.com/yugabyte/yugabyte-db/issues/10044)] [DST] PITR - Fix race in snapshot/schedule cleanup
* [[10051](https://github.com/yugabyte/yugabyte-db/issues/10051)] [DocDB] use RETURN_NOT_OK on an unchecked status
* [[10071](https://github.com/yugabyte/yugabyte-db/issues/10071)] Fix Locking Issues with DeleteTableInMemory
* [[10072](https://github.com/yugabyte/yugabyte-db/issues/10072)] [YSQL] Check the return status of certain YB functions
* [[10077](https://github.com/yugabyte/yugabyte-db/issues/10077)] [DocDB] Compaction file filter factory uses HistoryRetention instead of Schema
* [[10082](https://github.com/yugabyte/yugabyte-db/issues/10082)] Clean up environment on SetupUniverseReplication failure
* [[10085](https://github.com/yugabyte/yugabyte-db/issues/10085)] YSQL fix FATAL caused by wrong sum pushdown
* [[10098](https://github.com/yugabyte/yugabyte-db/issues/10098)] [YSQL] Fix index creation on temp table via ALTER TABLE
* [[10110](https://github.com/yugabyte/yugabyte-db/issues/10110)] [DocDB] Enables compaction file filter during manual compactions
* [[10111](https://github.com/yugabyte/yugabyte-db/issues/10111)] [YSQL] Import Force NO SCROLL for plpgsql's implicit cursors.
* [[10120](https://github.com/yugabyte/yugabyte-db/issues/10120)] [DocDB] added safe version of FastDecodeSignedVarInt
* [[10121](https://github.com/yugabyte/yugabyte-db/issues/10121)] [YSQL] Import Avoid misbehavior when persisting a non-stable cursor.
* [[10139](https://github.com/yugabyte/yugabyte-db/issues/10139)] [YBase] Avoid unnecessary table locking in CatalogManager::DeleteYsqlDBTables
* [[10164](https://github.com/yugabyte/yugabyte-db/issues/10164)] [DocDB] Max file size for compactions should only affect TTL tables
* [[10166](https://github.com/yugabyte/yugabyte-db/issues/10166)] Acquire lock in GetUniverseParamsWithVersion
* [[10167](https://github.com/yugabyte/yugabyte-db/issues/10167)] [DocDB] Save source tablet mutations to sys catalog when splitting
* [[10199](https://github.com/yugabyte/yugabyte-db/issues/10199)] [YSQL] Import Reset memory context once per tuple in validateForeignKeyConstraint.
* [[10211](https://github.com/yugabyte/yugabyte-db/issues/10211)] [xCluster] Allow for overriding the default CDCConsumerHandler threadpool size
* [[10218](https://github.com/yugabyte/yugabyte-db/issues/10218)] CheckLocalHostInMasterAddresses should check all specified RPC addresses
* [[10254](https://github.com/yugabyte/yugabyte-db/issues/10254)] [YSQL] Fix 100% CPU usage regression bug in SELECT with FOR KEY SHARE/IN/missing keys
* [[10304](https://github.com/yugabyte/yugabyte-db/issues/10304)] [DocDB] fix deadlock in ProcessTabletReportBatch
* [[10317](https://github.com/yugabyte/yugabyte-db/issues/10317)] [YSQL] Import `Allow users with BYPASSRLS to alter their own passwords.`
* [[10364](https://github.com/yugabyte/yugabyte-db/issues/10364)] [YCQL] Fix issue when dropping col that is not in an existing non-partial secondary index
* [[10374](https://github.com/yugabyte/yugabyte-db/issues/10374)] [YSQL] Cannot start a cluster with --ysql_pg_conf_csv='statement_timeout=1000'
* [[10415](https://github.com/yugabyte/yugabyte-db/issues/10415)] [backup] Backup-restore failures for old backups.
* [[10519](https://github.com/yugabyte/yugabyte-db/issues/10519)] Reset master leader on meta cache timeouts, refresh master leader after election.
* [adhoc] [DocDB] Remove GetTabletPeers method with return argument
* [adhoc] [DST] Reword loud log line in raft_consensus.cc to remove the word Failure
* [xCluster] [[9418](https://github.com/yugabyte/yugabyte-db/issues/9418)] Add cdc_state Schema Caching to Producer Cluster
* [YBase] Properly pass number of tables via MetricPrometheusOptions
* [YSQL] [[9572](https://github.com/yugabyte/yugabyte-db/issues/9572)] Correctly determine is_yb_relation for row-marked relations when preparing target list
* [YSQL] Change file and function names to match Yugabyte convention in catalog and access directories
* [YSQL] Import Fix performance bug in regexp's citerdissect/creviterdissect.
* Add S3 Bucket Host Base as endpoint in case of S3 compatible storage
* Fixed bug in yb-ctl for stopping processes, when os.kill raises an exception
* Increase column length for availability zone subnets

### Known issues

#### Yugabyte Platform

N/A

#### Database

N/A

## Notes

{{< note title="New release versioning" >}}

Starting with v2.2, Yugabyte release versions follow a [new release versioning convention](../../versioning). The latest release series, denoted by `MAJOR.ODD`, incrementally introduces new features and changes and is intended for development and testing only. Revision releases, denoted by `MAJOR.ODD.REVISION` versioning, can include new features and changes that might break backwards compatibility. For more information, see [Supported and planned releases](../../releases-overview).

{{< /note >}}
