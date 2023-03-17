Added us-gov-east-1 region in aws metadata
Adding debug hooks to add to container startup
Changing Null Check for pullsecretfile
[CLOUDGA-11585] CDC node should pass in the right AMI value
[DocDB] Renaming AbortedSubTransactionSet to SubtxnSet
Fix catchKeysWithNoMetadata UT failure on master
Fix spacing in pg_yb_utils.*
Fix swaggerGen
Fixing a buggy format statement
Import Odyssey (PostgreSQL connection pooler)
[15626] Move mac release builds to mac12 nodes
[15942] build: handle non-canonical thirdparty dir
[15830] [Backups] Added retry-loop for live TS searching
[13891] [Backfill] Do not update index permissions when table is being deleted
[15863] [Backfill] Fixed race in Tablet::FlushWriteIndexBatch
[3943] [YSQL] Support renaming constraints
[7376] [YSQL] track catalog version in pgstat
[10595] [YSQL] Add progress reporting for create index commands
[10651] [YSQL] System catalog upgrade
[10696] [YSQL] Avoid reading sys catalog from followers
[12417] [YSQL] Enhance GetTserverCatalogVersionInfo
[12884] [15938] [YSQL] Fix TSAN issues caused by quickdie()
[13123] [YSQL] Push down nextval() validation
[13132] [YSQL] Colocation: Handle failed CREATE TABLEGROUP
[13262] [YSQL] Upgrade: Replacing system view should affect other session's cache
[13358] [YSQL] DDL Atomicity Part 2 - YSQL reports DDL transaction status to YB-Master
[13467] [YSQL] Fix ANALYZE error after ALTER TABLE DROP COLUMN
[13748] [YSQL] Multiple request boundaries
[13868] [YSQL] Import Fix incorrect uses of Datum conversion macros
[13888] [YSQL] Fix expected test output in regression test org.yb.pgsql.TestPgRegressTablegroup.testPgRegressTablegroup
[14814] [YSQL] Exception handling in pushdown framework
[14815] [YSQL] Thread leak in Postgres process, created by yb::pggate::PgApiImpl::Interrupter::Start()
[14916] [YSQL] Enhance Test Coverage for Colocation
[14961] [14962] [YSQL] Further reduce first-query metadata RPCs
[15268] [YSQL] Optimize building of ybctid on pggate side
[15364] [YSQL] Add error message when using SPLIT clause with colocated indexes
[15359] [YSQL] Enhance `\h` for PROFILE commands
[15429] [YSQL] Fix regex in Test YSQL Upgrade migrationFilenameComment
[15610] [YSQL] fix flaky test: TestPgParallelSelect.testParallelWherePushdowns
[15444] [YSQL] Expose lock information to postgreSQL
[15743] [YSQL] pg_cast must be prefetched in case partitioned tables exists
[15745] [YSQL] Colocated Partitioned Table Backup && Restore Fails
[15761] [YSQL] Avoid caching responses for auth process
[15761] [YSQL] Reuse arena in PgSysTablePrefetcher
[15795] [YSQL] Adjust scan bounds to exclude NULL's on constrained columns
[15795] [YSQL] Disable yb_bypass_cond_recheck by default
[15820] [YSQL] fix ybgin index scan after DROP COLUMN
[15827] [YSQL] Reads in SERIALIZABLE isolation were not following Wait-on-Conflict concurrency control even when enable_wait_queues=true
[15186] [YSQL] expression pushdown forward compatibility
[13888] [YSQL] flaky test org.yb.pgsql.TestPgRegressTablegroup.testPgRegressTablegroup
[14395] [YSQL] PG Caches are not cleared properly if Rename Table operation fails.
[14239] [YSQL] Handle duplicate YSQL CREATE TABLEs on master using tables_
[3681] [DocDB] Remove dfatal log when the master->tserver AddTableToTablet RPC is retried by the master.
[10326] [DocDB] Refactor logic to find suitable snapshot id.
[12421] [DocDB] Fix checkfail caused by racing index deletion and query on a colocated db
[12581] [DocDB] TabletSplitITest/TabletSplitITestWithIsolationLevel.SplitSingleTablet
[12631] [DocDB] [YSQL] Update PGTableDesc table cache when partition list version is outdated
[13379] [DocDB] Fix BlockBasedTable memtracker being unregistered in Tablet destructor
[13715] [DocDB] Create a singular iterator for all batched ybctid's in Index Scans
[13718] [DocDB] Add support for offset based key columns decoding
[13831] [DocDB] Disable recheck for conditions that are bound to DocDB
[15362] [DocDB] Reuse regular RocksDB upper bound for intents RocksDB
[15378] [DocDB] Adding support for LibFuzzer
[15443] [DocDB] Fix setting cotable id for non primary YSQL catalog tables
[15458] [DocDB] Optionally omit colocated table ids from GetTableLocations RPCs.
[15499] [DocDB] Enable lease revocation
[15528] [DocDB] Fix usage of Connection::outbound_data_being_processed_ during Shutdown
[15530] [DocDB] Fix RPC shutdown in Messenger and Heartbeater
[15531] [DocDB] Make RWOperationCounter Stop idempotent
[15629] [DocDB] Update last_received_current_leader sent from replica to leader by including de-duped messages as well
[15643] [DocDB] Convert latency metrics from wait-on-conflict code to microseconds
[15698] [DocDB] Handle null placement info due to faulty restores
[15558] [DocDB] Make IsTabletSplittingComplete wait for the tablet split manager to not be running.
[13852] [DocDB] Fix flaky test: org.yb.cql.TestMasterVTableMetrics.testMasterMetrics
[13897] [DocDB] AutoFlag Upgrade Test fix
[14452] [DocDB] Tooling to List Database Names and UUIDs
[15051] [DocDB] Allow named constants in auto flags
[15051] [DocDB] Enable row packing for YSQL via auto flags
[15075] [DocDB] Return dns name, if available, for master server
[15093] [DocDB] Removing -flto flag from .o generation in prof_gen build.
[15183] [DocDB] fix gcc builds
[15183] [DocDB] move list of table ids from colocated tablet's system catalog entry to a field on each child table's sys catalog entry pointing to its parent
[15220] [DocDB] Fix apply-operation timeout during schema changes
[14138] [DocDB] WAL files are regenerated if the tserver gets into a crashloop
[14326] [DocDB] Fixed Missing Argument Expressions for yb-admin
[14569] [DocDB] fix conflict detection in colocated table for row lock
[14677] [DocDB] Fix lock inversion in master/snapshot code path
[14709] [DocDB] Enable deadlock detection in tsan
[14853] [DocDB] Deprecate tserver_tcmalloc_max_total_thread_cache_bytes
[14998] [13331] [DocDB] Fix data race of mem_table_flush_filter_factory_ in Bootstrap [TSan Unit Test]
[15008] [DocDB] fix flaky test RaftConsensusITest.TestAddRemoveNonVoter
[13485] Master Issues: Added div to Encryption Status field to prevent alignment breaks
[13801] [yugabyted] code changes to handle RF-5 deployments with configure command.
[14567] [yugabyted] Adding support for YSQL_hba_conf_csv and ysql_pg_conf_csv to '--tserver_flags' flag of 'yugabyted start' command
[14440] [yugabyted] support for proving any node ip-address for --join flag during cluster creation
[14684] [yugabyted] Support for '~' in paths given through CLI or conf file.
[16160] [yugabyted] Stopping the processes properly in MacOS.
[16245] [yugabyted] Fixing `yugabyted configure data_placement` for nodes started with DNS name as `--advertise_address`
[15692] [yugabyted] Code changes to add DNS validation when start command is provided with a DNS name
[15723] [yugabyted] code changes to fix enable encrypt-at-rest command.
[14739] [yugabyted] 'yugabyted configure' should work only when node is running
[15898] [yugabyted] Rearrange the output of 'yugabyted start' command

[14043] [DST] PITR - Handle txn ddl gc in conjunction with PITR
[15808] [xCluster] Throttle External Transaction Apply RPCs
[16018] [xCluster] Add null check for Consensus in cdc_producer
[16128] [xCluster] Adding DR test for xCluster
[16179] [xCluster] Abort active txn during Bootstrap
[16238] [xCluster] Dont compute batch size in BatchedWriteImplementation::ProcessRecord if no limit is set
[12750] [xCluster] Log TryAgain errors as warnings in TwoDCOutputClient
[14268] [xCluster] Fix GetChangesOnSplitParentTablet
[14374] [xCluster] Handle Bootstrap of transaction status table
[14462] [xCluster] Block dropping YSQL databases that contain replicated tables
[14635] [xCluster] Disable cleanup of uncommitted intents on the compaction path
[15542] [xCluster] Pause/resume replication of user and system replication groups simultaneously
[15841] [xCluster] GC Transactions older than 24hrs on the Consumer Coordinator
[15622] [xCluster] Change CHECK to Status in TransactionParticipant Add path
[xCluster] Per-table replication status is not set properly after sync
[15687] [xCluster] Fix issue with Compaction and TSAN warning
[14527] [CDCSDK] Flaky ctest ActiveAndInActiveStreamOnSameTablet Fix
[15516] [CDCSDK] Data loss with CDC snapshot plus streaming
[15562] [CDCSDK] In cases of not using the before image remove safe time after a successful snapshot
[15584] [CDCSDK] Populate commit_time, record_time for each record and safepoint to each batch of GetChanges
[16047] [CDCSDK] Mark stream as active on ns with no table with PK
[16120] [CDCSDK] Add test for schema evolution with multiple streams
[16147] [CDCSDK] Checkpoint set to 0.0 from raft superblock in upgrade scenarios
[15705] [CDCSDK] Update CDCSDK checkpoint non-actionable replicated messages in WAL
[CDCSDK] Add issue template for CDC issues (#15556)
[15752] [CDCSDK] Continue if before image is missing in PopulateBeforeImage in packed row
[15759] [CDCSDK] Add a parameter to SetCheckpointRequest to accept cdc_sdk_safe_time
[15824] [CDCSDK] Removes unused header_schema and header_schema_version fields from ReadOpsResult
[CDC] Add new APIs for creating restricted user and removing users

[15735] [DocDB] Add Raft Config info to consensus information in YB-Tserver UI
[15735] [DocDB] Add tserver UUID to RaftConfig UI of YB-Master in per-table view
[15763] [DocDB] Fix reading packed row with column updated to NULL
[15774] [DocDB] Disabled TestRedisServiceExternal.TestSlowSubscribersSoftLimit
[15777] [DocDB] Fix flaky AutomaticTabletSplittingMultiPhase test
[15786] [DocDB] Make DocRowwiseIterator hold shared_ptr of DocReadContext for its lifetime
[15807] [DocDB] Ensure IntraTxnWriteId is properly decoded even in weak lock intents

[15830] [DocDB] Flaky test PgMiniTest.NonRespondingMaster
[15829] [DocDB] fix leader ht lease exp under leader only mode
[15839] [DocDB] Fixes flaky test org.yb.cql.TestTransaction.testTimeout
[15842] [DocDB] Count time from transaction init for tracing thresholds
[15846] [DocDB] fix flaky test: SnapshotTest.SnapshotRemoteBootstrap
[15883] [DocDB] Check permission status of YCQL indices in `CatalogManager::IsDeleteTableDone`
[15887] [DocDB] Introduce the ability to tag the stateful service type on the tablet
[15891] [DocDB] Fix race condition in TestPgIsolationRegress#isolationRegressWithWaitQueues
[15849] [DocDB] Don't hold mutex when starting TransactionStatusResolver
[15869] [DocDB] Add read replica blacklist tests.
[15947] [DocDB] Pass index_map as a shared pointer to WriteQuery::QLWriteOperation
[15957] [DocDB] Error message grammar
[15958] [DocDB] fix memory leak in KeyEntryValue::DecodeKey/PrimitiveValue::DecodeFromValue
[15996] [DocDB] Disabled TestRedisService.TestTtlSet and TestRedisService.TestTtlSortedSet
[16015] [DocDB] Make ysql_enable_packed_row a gflag
[16027] [DocDB] Add optional logging on transaction error
[16028] [DocDB] Move read_time_wait metric to be table-level
[16056] [DocDB] Add master GetStatefulServiceLocation rpc to resolve the location of StatefulServices
[16057] [DocDB] Make hosted_services an unordered_set
[16061] [DocDB] Fixes memory leak issues in RemoteBootstrapAnchorClient
[16088] [DocDB] Use correct errno in log for failed mmap.
[16109] [DocDB] Fix deadlock with YqlPartitionsVTable
[16140] [DocDB] Bump RBS session default timeout
[16145] [DocDB] Move backup out of ent folder
[16151] [DocDB] Fix incorrect iteration over modified set in CleanupAbortsTask::Run
[16173] [DocDB] Cluster creation is failing on Mac
[16180] [DocDB] Cleanup ent/server
[16345] [DocDB] Disable WAL reuse at bootstrap
[16199] [DocDB] Bring CatalogManager::CreateTable under the linter length limit.
[15911] [15912] [DocDB] Fix transaction promotion/conflict resolution abort handling
[15840] [YSQL] Preload `pg_proc` catalog in the case of partitioned tables
[15873] [YSQL] Fix Test ysql Upgrade failures
[15878] [YSQL] Restore root->yb_availBatchedRelids after its use.
[15881] [YSQL] unpushable SET clause expressions caused unnecessary index updates
[15892] [YSQL] Fix bug in how empty bitmap sets are detected in batched nested loop planning
[15894] [YSQL] Turn on Colocation GA Behavior by default
[15919] [YSQL] Enable initial system catalog generation for TSAN builds
[15928] [15923] [15925] [YSQL] Convert some PG variables to std::atomic variables to prevent TSAN failures.
[15939] [YSQL] Import 'Switch flags tracking pending interrupts to sig_atomic_t' to fix TSAN failures
[15952] [YSQL] Reduce verbosity of a Postgres log line in read committed isolation level codepath
[15959] [YSQL] Fix migrations V17, V22, V31 to properly update pg_proc
[15962] [YSQL] Java tests in TSAN build fail due to process_tree_supervisor.py failing to kill all processes
[15969] [YSQL] Control inclusiveness info being sent over in QL_OP_BETWEEN with an AutoFlag
[15977] [YSQL] Import PostgreSQL commits for log_statement_sample_rate and log_min_duration_sample
[16004] [YSQL] Colocation GA alter table add primary key, drop constraint pk not working for colocated tables
[16011] [YSQL] Fix memory leak in pg_doc_op
[16012] [YSQL] Add to the correct side of yb_availBatchedRelids and allow BNL to work on Relabeled Vars
[16020] [YSQL] Fix yb_get_range_split_clause primary key constraint wrong assumption
[16044] [YSQL] Correct warning message on using tablegroup and old colocated syntax
[16045] [YSQL] Import PostgreSQL commits for log_transaction_sample_rate
[16046] [YSQL] YbPreloadCatalogCache simplification
[16048] [YSQL] Fix regression in performance of queries returning large columns
[16034] [15933] [YSQL] Avoid picking hybrid timestamps in YSQL backends
[16062] [YSQL] Avoid returning raw pointer from YBClient::NewNamespaceAlterer
[16077] [YSQL] fixed kGroupEnd bug for range scans containing IN clause and strict inequality
[16082] [YSQL] ysql_dump cannot correctly dump primary key with INCLUDE clause for table schema
[16204] [YSQL] Fix expected error messages not printed bug
[15874] [YCQL] Fix wrong metadata version with pagination on SELECT using Index
[16135] [YCQL] Use Metadata Cache in IsYBTableAltered
[15367] [YCQL] Support deletion from List/Map in DELETE stmt
[DB-4176] [YCQLSH] Updating yugabyte-db-thirdparty release version
[DB-4390] Fix CVE vulnerabilities
[DB-4393] [15210] YBClient fails to connect if the tserver advertises multiple hosts/ports
[DEVOPS-2244] Spark: Add fail tag for tsan deadlock detection


