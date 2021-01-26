---
title: 2.4.0 Stable Release Series
headerTitle: 2.4.0 Stable Release Series
linkTitle: v2.4.0 (stable)
description: Enhancements, changes, and resolved issues in the current stable release series recommended for production deployments.
headcontent: Features, enhancements, and resolved issues in the current stable release series recommended for production deployments.
aliases:
  - /latest/releases/
menu:
  latest:
    identifier: stable-releases
    parent: whats-new
    weight: 2586
isTocNested: true
showAsideToc: false  
---




# Release Notes

# v2.4.0 - Jan 22, 2021

### New Features

##### Yugabyte Platform

- Data encryption in-transit enhancements:

  - Support for bringing your own CA certificates for an on-premise cloud provider ([#5729](https://github.com/yugabyte/yugabyte-db/issues/5729))
  - Support for rotating custom CA certificates ([#5727](https://github.com/yugabyte/yugabyte-db/issues/5727))
  - Displaying the certificate name in the YB Platform Universe editor
  - Moving of the Certificates page to Cloud Config > Security > Encryption in Transit

- Moving of TLS In-Transit Certificates to Cloud > Security Config

- Support for Rolling restart of a universe ([#6323](https://github.com/yugabyte/yugabyte-db/issues/6323))

- Support for VMware Tanzu as a new cloud provider ([#6633](https://github.com/yugabyte/yugabyte-db/issues/6633))

- Alerts for backup tasks ([#5556](https://github.com/yugabyte/yugabyte-db/issues/5556))

##### Core Database

- General availability of [Automatic Tablet splitting](https://docs.yugabyte.com/latest/architecture/docdb-sharding/tablet-splitting/), which enables resharding of data in a cluster automatically while online, and transparently to users, when a specified size threshold has been reached ([#1004](https://github.com/yugabyte/yugabyte-db/issues/1004))
- Introducing support for audit logging in YCQL and YSQL API ([#1331](https://github.com/yugabyte/yugabyte-db/issues/1331),[ #5887](https://github.com/yugabyte/yugabyte-db/issues/5887), [#6199](https://github.com/yugabyte/yugabyte-db/issues/6199))
- Ability to log slow running queries in YSQL ([#4817](https://github.com/YugaByte/yugabyte-db/issues/4817))
- Introducing support for LDAP integration in YSQL API ([#6088](https://github.com/yugabyte/yugabyte-db/issues/6088))

### Improvements

##### Yugabyte Platform

- Support for Transactional tables and Cross Cluster Async Replication topology ([#5779](https://github.com/yugabyte/yugabyte-db/issues/5779))
- Support for very large transactions and stability improvements ([#1923](https://github.com/yugabyte/yugabyte-db/issues/1923))
- Displaying an entire query in the detailed view of the live queries tab ([#6412](https://github.com/yugabyte/yugabyte-db/issues/6412))
- Not returning Hashes and Tokens in API responses ([#6388](https://github.com/yugabyte/yugabyte-db/issues/6388))
- Authentication of proxy requests against a valid platform session ([#6544](https://github.com/yugabyte/yugabyte-db/issues/6544))
- Disabling of the unused API endpoint run_query ([#6383](https://github.com/yugabyte/yugabyte-db/issues/6388))
- Improved error handling for user-created concurrent backup tasks ([#5888](https://github.com/yugabyte/yugabyte-db/issues/5888))
- Performance improvements for live queries ([#6289](https://github.com/yugabyte/yugabyte-db/issues/6289))
- Pre-flight checks for Create Universe, Edit Universe, and Add Node operations for an on-premise provider ([#6016](https://github.com/yugabyte/yugabyte-db/issues/6016))
- Disabling of the unused API endpoint run_in_shell ([#6384](https://github.com/yugabyte/yugabyte-db/issues/6384))
- Disabling of the unused API endpoint create_db_credentials ([#6385](https://github.com/yugabyte/yugabyte-db/issues/6385))
- Input validation for Kubernetes configuration path ([#6389](https://github.com/yugabyte/yugabyte-db/issues/6389))
- Input validation for the access_keys API endpoint ([#6386](https://github.com/yugabyte/yugabyte-db/issues/6386))
- Input path validation for backup target paths ([#6382](https://github.com/yugabyte/yugabyte-db/issues/6382))
- Timeout support for busybox ([#6652](https://github.com/yugabyte/yugabyte-db/issues/6652))
- Enabling of CORS policy by default ([#6390](https://github.com/yugabyte/yugabyte-db/issues/6390))

- Deleting backups for TLS-enabled universes ([#5980](https://github.com/yugabyte/yugabyte-db/issues/5980))

##### Core Database

- DDL consistency ([#4710](https://github.com/yugabyte/yugabyte-db/issues/4710),[ #3979](https://github.com/yugabyte/yugabyte-db/issues/3979),[ #4360](https://github.com/yugabyte/yugabyte-db/issues/4360))

- DDL performance improvements ([#5177](https://github.com/yugabyte/yugabyte-db/issues/5177), [#3503](https://github.com/yugabyte/yugabyte-db/issues/3503))

- ALTER versions for ORM support ([#4424](https://github.com/yugabyte/yugabyte-db/issues/4424))

- Online index backfill GA ([#2301](https://github.com/yugabyte/yugabyte-db/issues/2301), [#4899](https://github.com/yugabyte/yugabyte-db/issues/4899), [#5031](https://github.com/yugabyte/yugabyte-db/issues/5031))

- Table partitioning ([#1126](https://github.com/yugabyte/yugabyte-db/issues/1126),[ #5387](https://github.com/yugabyte/yugabyte-db/issues/5387))

- SQL support improvements:

  - Support for the FROM clause in UPDATE ([#738](https://github.com/YugaByte/yugabyte-db/issues/738))
  - Support for the USING clause in DELETE ([#738](https://github.com/YugaByte/yugabyte-db/issues/738))
  - SQL/JSON path language [PG12] ([#5408](https://github.com/YugaByte/yugabyte-db/issues/5408))

- Performance improvements:

  - Sequence cache min-value flag ([#6041](https://github.com/yugabyte/yugabyte-db/issues/6041),[ #5869](https://github.com/yugabyte/yugabyte-db/issues/5869))
  - Foreign-key batching ([#2951](https://github.com/yugabyte/yugabyte-db/issues/2951))

- YSQL usability improvements:

  - COPY FROM rows-per-txn option ([#2855](https://github.com/yugabyte/yugabyte-db/issues/2855), [#6380](https://github.com/yugabyte/yugabyte-db/issues/6380))
  - COPY / ysql_dump OOM ([#5205](https://github.com/yugabyte/yugabyte-db/issues/5205), [#5453](https://github.com/yugabyte/yugabyte-db/issues/5453), [#5603](https://github.com/yugabyte/yugabyte-db/issues/5603))
  - Large DML OOM ([#5374](https://github.com/yugabyte/yugabyte-db/issues/5374))
  - Updating pkey values ([#659](https://github.com/yugabyte/yugabyte-db/issues/659))
  - Restarting write-txns on conflict ([#4291](https://github.com/yugabyte/yugabyte-db/issues/4291))

- YSQL statement execution statistics ([#5478](https://github.com/yugabyte/yugabyte-db/issues/5478))

- Improved raft leader stepdown operations, by waiting for the target follower to catch up. This reduces the unavailability window during cluster operations. ([#5570](https://github.com/YugaByte/yugabyte-db/issues/5570))

- Improved performance during cluster overload:

  - Improved throttling for user-level YCQL RPC ([#4973](https://github.com/yugabyte/yugabyte-db/issues/4973))
  - Lower YCQL live RPC memory usage ([#5057](https://github.com/yugabyte/yugabyte-db/issues/5057))
  - Improved throttling for internal master level RPCs ([#5434](https://github.com/yugabyte/yugabyte-db/issues/5434))

- Improved performance for YCQL and many connections:

  - Cache for generic YCQL system queries ([#5043](https://github.com/yugabyte/yugabyte-db/pull/5043))
  - Cache for auth information for YCQL ([#6010](https://github.com/yugabyte/yugabyte-db/issues/6010))
  - Speedup system.partitions queries ([#6394](https://github.com/yugabyte/yugabyte-db/issues/6394))

- Improved DNS handling:

  - Process-wide cache for DNS queries ([#5201](https://github.com/yugabyte/yugabyte-db/issues/5201))
  - Avoid duplicate DNS requests during YCQL system.partitions queries ([#5225](https://github.com/yugabyte/yugabyte-db/issues/5225))

- Improved master-level load balancer (LB) operations:

  - Throttle tablet moves per table ([#4053](https://github.com/yugabyte/yugabyte-db/issues/4053))
  - New global optimization for tablet balancing -- this also allows the LB to work with colocation ([#3079](https://github.com/yugabyte/yugabyte-db/issues/3079))
  - New global optimization for leader balancing ([#5021](https://github.com/yugabyte/yugabyte-db/issues/5021))
  - Delay LB on master leader failover ([#5221](https://github.com/yugabyte/yugabyte-db/issues/5221))
  - Register temporary TS on master failover to prevent invalid LB operations ([#4691](https://github.com/yugabyte/yugabyte-db/issues/4691))
  - Configurable option to disallow TLS v1.0 and v1.1 protocols ([#6865](https://github.com/yugabyte/yugabyte-db/issues/6865))

- Skip loading deleted table metadata into master memory ([#5122](https://github.com/yugabyte/yugabyte-db/issues/5122))

### Bug Fixes

##### Yugabyte Platform

- Edit Universe did not display the TLS certificate used
- Multi-zone K8s universe creation failed ([#5882](https://github.com/yugabyte/yugabyte-db/issues/5882))
- Tasks page reported an incorrect status of a failed backup ([#6210](https://github.com/yugabyte/yugabyte-db/issues/6210))
- K8s universe upgrade did not wait for each pod to update before moving to the next ([#6360](https://github.com/yugabyte/yugabyte-db/issues/6360))
- Changing node count by AZ was doing a full move ([#5335](https://github.com/yugabyte/yugabyte-db/issues/5335))
- Enabling Encryption-at-Rest without KMS configuration caused create Universe to fail silently ([#6228](https://github.com/yugabyte/yugabyte-db/issues/6228))
- Missing SST size in the Nodes page of a universe with read replicas ([#6275](https://github.com/yugabyte/yugabyte-db/issues/6275))
- Incorrect link in the Live queries dashboard node name ([#6458](https://github.com/yugabyte/yugabyte-db/issues/6458))
- Failure to create a TLS-enabled universe with a custom home directory setting with on-premise provider ([#6602](https://github.com/yugabyte/yugabyte-db/issues/6602))
- Database node health liveliness check was blocked indefinitely ([#6301](https://github.com/yugabyte/yugabyte-db/issues/6301))

##### Core Database

- Critical fixes for transaction cleanup applicable to aborted transactions (observed frequently as servers reaching soft memory limit)

- Main raft fixes:

  - Incorrect tracking of flushed op id in case of aborted operations ([#4150](https://github.com/yugabyte/yugabyte-db/issues/4150))
  - Various fixes and potential crashes on tablet bootstrap ([#5003](https://github.com/yugabyte/yugabyte-db/issues/5003), [#3759](https://github.com/yugabyte/yugabyte-db/issues/3759), [#4983](https://github.com/yugabyte/yugabyte-db/issues/4983), [#5215](https://github.com/yugabyte/yugabyte-db/issues/5215), [#5224](https://github.com/yugabyte/yugabyte-db/issues/5224))
  - Avoid running out of threads due to FailureDetector ([#5752](https://github.com/yugabyte/yugabyte-db/issues/5752))

- Metrics reporting could be inconsistent due to regular and intent rocksdb using the same statistics object ([#5640](https://github.com/yugabyte/yugabyte-db/issues/5640))

- Various issues with the rocksdb snapshot mechanism used for backup/restore ([#6170](https://github.com/yugabyte/yugabyte-db/issues/6170), [#4756](https://github.com/yugabyte/yugabyte-db/issues/4756), [#5337](https://github.com/yugabyte/yugabyte-db/issues/5337))

- Fixes in the YCQL API:

  - Fix parser typo when scanning binary values ([#6827](https://github.com/yugabyte/yugabyte-db/issues/6827))
  - Omit where clause if there is no range column ([#6826](https://github.com/yugabyte/yugabyte-db/issues/6826))
  - Fix JSONB operator execution when applying to NULL objects ([#6766](https://github.com/yugabyte/yugabyte-db/issues/6766))
  - Fix range deletes to start and end at specified primary key bounds ([#6649](https://github.com/yugabyte/yugabyte-db/issues/6649))

- Fixes in the YSQL API:

  - Fix crash for large DDL transaction ([#6430](https://github.com/yugabyte/yugabyte-db/issues/6430))
  - Fixed SIGSERV in YBPreloadRelCache ([#6317](https://github.com/yugabyte/yugabyte-db/issues/6317))
  - Fix backup-restore issues when source table was altered before ([#6009](https://github.com/YugaByte/yugabyte-db/issues/6009), [#6245](https://github.com/YugaByte/yugabyte-db/issues/6245), [#5958](https://github.com/YugaByte/yugabyte-db/issues/5958))
  - Fix backup-restore failure due to unexpected NULLABLE column attribute. ([#6886](https://github.com/yugabyte/yugabyte-db/issues/6886))

- Retry on SSL_ERROR_WANT_WRITE ([#6266](https://github.com/yugabyte/yugabyte-db/issues/6266))

### Known Issues

##### Yugabyte Platform

- Azure IaaS orchestration (in beta status):

  - No pricing information ([#5624](https://github.com/yugabyte/yugabyte-db/issues/5624))
  - No support for regions with zero availability zones (AZs) ([#5628](https://github.com/yugabyte/yugabyte-db/issues/5628))

##### Core Database

- Automatic Tablet Splitting:

  - While a tablet split is occurring, in-flight operations for both YCQL and YSQL APIs would currently receive errors. These would currently have to be retried at the application level currently. In the future, these will be transparently handled underneath the hood. The immediate impact for this would be that certain tools like TPCC or sysbench would fail while tablet splitting is happening.


### Notes

{{< note title="Release versioning" >}}

Starting with release 2.2.0, Yugabyte follows a [new release versioning convention](../../versioning). Stable release series are denoted by `MAJOR.EVEN`, introduce new features and changes added since the previous stable release series; these releases are supported for production deployments. Patch releases are denoted by `MAJOR.EVEN.PATCH`, include bug fixes and revisions that do not break backwards compatibility.
For more information, see [Releases overview](../../releases-overview).

{{< /note >}}

{{< note title="Upgrading from release 1.3" >}}

Prior to release 2.0, YSQL was in beta. Since release 2.0, a backward-incompatible file format change was made for YSQL. For existing clusters running pre-2.0 release with YSQL enabled, you cannot upgrade to release 2.0 or later. Instead, export your data from existing clusters and then import the data into a new cluster (2.0 or later).

{{< /note >}}