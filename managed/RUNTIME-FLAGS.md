# <u>List of supported Runtime Configuration Flags</u>
### These are all the public runtime flags in YBA.
| Name | Key | Scope | Help Text | Data Type |
| :----: | :----: | :----: | :----: | :----: |
| "Task Garbage Collection Retention Duration" | "yb.taskGC.task_retention_duration" | "CUSTOMER" | "We garbage collect stale tasks after this duration" | "Duration" |
| "Enforce Auth" | "yb.universe.auth.is_enforced" | "CUSTOMER" | "Enforces users to enter password for YSQL/YCQL during Universe creation" | "Boolean" |
| "Enable dedicated nodes" | "yb.ui.enable_dedicated_nodes" | "CUSTOMER" | "Gives the option to place master and tserver nodes separately during create/edit universe" | "Boolean" |
| "Max Number of Customer Tasks to fetch" | "yb.customer_task_db_query_limit" | "CUSTOMER" | "Knob that can be used when there are too many customer tasks overwhelming the server" | "Integer" |
| "Show costs in UI" | "yb.ui.show_cost" | "CUSTOMER" | "Option to enable/disable costs in UI" | "Boolean" |
| "Helm chart http download timeout" | "yb.releases.download_helm_chart_http_timeout" | "CUSTOMER" | "The timeout for downloading the Helm chart while importing a release using HTTP" | "Duration" |
| "Use Redesigned Provider UI" | "yb.ui.feature_flags.provider_redesign" | "CUSTOMER" | "The redesigned provider UI adds a provider list view, a provider details view and improves the provider creation form for AWS, AZU, GCP, and K8s" | "Boolean" |
| "Enable partial editing of in use providers" | "yb.ui.feature_flags.edit_in_use_provider" | "CUSTOMER" | "A subset of fields from in use providers can be edited. Users can edit in use providers directly through the YBA API. This config is used to enable this functionality through YBA UI as well." | "Boolean" |
| "Show underlying xCluster configs from DR setup" | "yb.ui.xcluster.dr.show_xcluster_config" | "CUSTOMER" | "YBA creates an underlying transactional xCluster config when setting up an active-active single-master disaster recovery (DR) config. During regular operation you should manage the DR config through the DR UI instead of the xCluster UI. This feature flag serves as a way to expose the underlying xCluster config for troubleshooting." | "Boolean" |
| "Enforce User Tags" | "yb.universe.user_tags.is_enforced" | "CUSTOMER" | "Prevents universe creation when the enforced tags are not provided." | "Boolean" |
| "Enforced User Tags List" | "yb.universe.user_tags.enforced_tags" | "CUSTOMER" | "A list of enforced user tag and accepted value pairs during universe creation. Pass '*' to accept all values for a tag. Ex: [\"yb_task:dev\",\"yb_task:test\",\"yb_owner:*\",\"yb_dept:eng\",\"yb_dept:qa\", \"yb_dept:product\", \"yb_dept:sales\"]" | "Key Value SetMultimap" |
| "Enable IMDSv2" | "yb.aws.enable_imdsv2_support" | "CUSTOMER" | "Enable IMDSv2 support for AWS providers" | "Boolean" |
| "Allow Unsupported Instances" | "yb.internal.allow_unsupported_instances" | "PROVIDER" | "Enabling removes supported instance type filtering on AWS providers." | "Boolean" |
| "Default AWS Instance Type" | "yb.aws.default_instance_type" | "PROVIDER" | "Default AWS Instance Type" | "String" |
| "Default GCP Instance Type" | "yb.gcp.default_instance_type" | "PROVIDER" | "Default GCP Instance Type" | "String" |
| "Default Azure Instance Type" | "yb.azure.default_instance_type" | "PROVIDER" | "Default Azure Instance Type" | "String" |
| "Default Kubernetes Instance Type" | "yb.kubernetes.default_instance_type" | "PROVIDER" | "Default Kubernetes Instance Type" | "String" |
| "Default AWS Storage Type" | "yb.aws.storage.default_storage_type" | "PROVIDER" | "Default AWS Storage Type" | "String" |
| "Default GCP Storage Type" | "yb.gcp.storage.default_storage_type" | "PROVIDER" | "Default GCP Storage Type" | "String" |
| "Default Azure Storage Type" | "yb.azure.storage.default_storage_type" | "PROVIDER" | "Default Azure Storage Type" | "String" |
| "Universe Boot Script" | "yb.universe_boot_script" | "PROVIDER" | "Custom script to run on VM boot during universe provisioning" | "String" |
| "Default AWS Volume Count" | "yb.aws.default_volume_count" | "PROVIDER" | "Default AWS Volume Count" | "Integer" |
| "Default AWS Volume Size" | "yb.aws.default_volume_size_gb" | "PROVIDER" | "Default AWS Volume Size" | "Integer" |
| "Default GCP Volume Size" | "yb.gcp.default_volume_size_gb" | "PROVIDER" | "Default GCP Volume Size" | "Integer" |
| "Default Azure Volume Size" | "yb.azure.default_volume_size_gb" | "PROVIDER" | "Default Azure Volume Size" | "Integer" |
| "Default Kubernetes Volume Count" | "yb.kubernetes.default_volume_count" | "PROVIDER" | "Default Kubernetes Volume Count" | "Integer" |
| "Default Kubernetes Volume Size" | "yb.kubernetes.default_volume_size_gb" | "PROVIDER" | "Default Kubernetes Volume Size" | "Integer" |
| "Default Kubernetes CPU cores" | "yb.kubernetes.default_cpu_cores" | "PROVIDER" | "Default Kubernetes CPU cores" | "Integer" |
| "Minimum Kubernetes CPU cores" | "yb.kubernetes.min_cpu_cores" | "PROVIDER" | "Minimum Kubernetes CPU cores" | "Integer" |
| "Maximum Kubernetes CPU cores" | "yb.kubernetes.max_cpu_cores" | "PROVIDER" | "Maximum Kubernetes CPU cores" | "Integer" |
| "Default Kubernetes Memory Size" | "yb.kubernetes.default_memory_size_gb" | "PROVIDER" | "Default Kubernetes Memory Size" | "Integer" |
| "Minimum Kubernetes Memory Size" | "yb.kubernetes.min_memory_size_gb" | "PROVIDER" | "Minimum Kubernetes Memory Size" | "Integer" |
| "Maximum Kubernetes Memory Size" | "yb.kubernetes.max_memory_size_gb" | "PROVIDER" | "Maximum Kubernetes Memory Size" | "Integer" |
| "Enable Node Agent Client" | "yb.node_agent.client.enabled" | "PROVIDER" | "Enable node agent client for communication to DB nodes." | "Boolean" |
| "Enable Ansible Offloading" | "yb.node_agent.ansible_offloading.enabled" | "PROVIDER" | "Offload ansible tasks to the DB nodes." | "Boolean" |
| "Remote tmp directory" | "yb.filepaths.remoteTmpDirectory" | "PROVIDER" | "A remote temporary directory should be used for performing operations on nodes within the provider scope." | "String" |
| "Polling interval for GCP Opertion status" | "yb.gcp.operations.status_polling_interval" | "PROVIDER" | "Interval to poll the status of an ongoing GCP resource creation operation." | "Duration" |
| "GCP Operation Timeout interval" | "yb.gcp.operations.timeout_interval" | "PROVIDER" | "Timeout interval to wait for GCP resource creation operations to complete sucessfully." | "Duration" |
| "Make YBC listen on 0.0.0.0" | "yb.ybc_flags.listen_on_all_interfaces_k8s" | "PROVIDER" | "Makes YBC bind on all network interfaces" | "Boolean" |
| "Azure Virtual Machine Params blob" | "yb.azure.custom_params.vm" | "PROVIDER" | "Custom JSON of Azure parameters to apply on top of virtual machine creation." | "String" |
| "Azure Disk Params blob" | "yb.azure.custom_params.disk" | "PROVIDER" | "Custom JSON of Azure parameters to apply on top of data disk creation." | "String" |
| "Azure Network Interface Params blob" | "yb.azure.custom_params.network" | "PROVIDER" | "Custom JSON of Azure parameters to apply on top of network interface creation." | "String" |
| "Ignore VM plan information" | "yb.azure.vm.ignore_plan" | "PROVIDER" | "Skip passing in any plan information when creating virtual machine, even if found." | "Boolean" |
| "Monitored mount roots" | "yb.provider.monitored_mount_roots" | "PROVIDER" | "Mount roots, which we show on the merics dashboard and which we're alerting on." | "String" |
| "Enable Geo-partitioning" | "yb.universe.geo_partitioning_enabled" | "PROVIDER" | "Enables geo-partitioning for universes created with this provider." | "Boolean" |
| "Enable YBC" | "ybc.provider.enabled" | "PROVIDER" | "Enable YBC for universes created with this provider" | "Boolean" |
| "Configure OpenTelemetry metrics port" | "yb.universe.otel_collector_metrics_port" | "PROVIDER" | "OpenTelemetry metrics port" | "Integer" |
| "Max Number of Parallel Node Checks" | "yb.health.max_num_parallel_node_checks" | "GLOBAL" | "Number of parallel node checks, spawned as part of universes health check process" | "Integer" |
| "Log Script Output For YBA HA Feature" | "yb.ha.logScriptOutput" | "GLOBAL" | "To log backup restore script output for debugging issues" | "Boolean" |
| "Use Kubectl" | "yb.use_kubectl" | "GLOBAL" | "Use java library instead of spinning up kubectl process." | "Boolean" |
| "Enable SSH2" | "yb.security.ssh2_enabled" | "GLOBAL" | "Flag for enabling ssh2 on YBA" | "Boolean" |
| "Enable Custom Hooks" | "yb.security.custom_hooks.enable_custom_hooks" | "GLOBAL" | "Flag for enabling custom hooks on YBA" | "Boolean" |
| "Enable SUDO" | "yb.security.custom_hooks.enable_sudo" | "GLOBAL" | "Flag for enabling sudo access while running custom hooks" | "Boolean" |
| "Enable API Triggered Hooks" | "yb.security.custom_hooks.enable_api_triggered_hooks" | "GLOBAL" | "Flag for enabling API Triggered Hooks on YBA" | "Boolean" |
| "Disable XX Hash Checksum" | "yb.backup.disable_xxhash_checksum" | "GLOBAL" | "Flag for disabling xxhsum based checksums for computing the backup" | "Boolean" |
| "Enable K8s Support Bundle" | "yb.support_bundle.k8s_enabled" | "GLOBAL" | "This config lets you enable support bundle creation on k8s universes." | "Boolean" |
| "Enable On Prem Support Bundle" | "yb.support_bundle.onprem_enabled" | "GLOBAL" | "This config lets you enable support bundle creation for onprem universes." | "Boolean" |
| "Allow collection of cores in Support Bundle" | "yb.support_bundle.allow_cores_collection" | "GLOBAL" | "This global config allows you to disable collection of cores in support bundle, even if it is passed as a component while creating." | "Boolean" |
| "Snapshot creation max attempts" | "yb.snapshot_creation.max_attempts" | "GLOBAL" | "Max attempts while waiting for AWS Snapshot Creation" | "Integer" |
| "Snapshot creation delay" | "yb.snapshot_creation.delay" | "GLOBAL" | "Delay per attempt while waiting for AWS Snapshot Creation" | "Integer" |
| "Runtime Config UI" | "yb.runtime_conf_ui.enable_for_all" | "GLOBAL" | "Allows users to view the runtime configuration properties via UI" | "Boolean" |
| "YBC Upgrade Interval" | "ybc.upgrade.scheduler_interval" | "GLOBAL" | "YBC Upgrade interval" | "Duration" |
| "YBC Universe Upgrade Batch Size" | "ybc.upgrade.universe_batch_size" | "GLOBAL" | "The number of maximum universes on which ybc will be upgraded simultaneously" | "Integer" |
| "YBC Node Upgrade Batch Size" | "ybc.upgrade.node_batch_size" | "GLOBAL" | "The number of maximum nodes on which ybc will be upgraded simultaneously" | "Integer" |
| "YBC Stable Release" | "ybc.releases.stable_version" | "GLOBAL" | "Stable version for Yb-Controller" | "String" |
| "YBC admin operation timeout" | "ybc.timeout.admin_operation_timeout_ms" | "GLOBAL" | "YBC client timeout in milliseconds for admin operations" | "Integer" |
| "Bootstrap producer timeout" | "yb.xcluster.bootstrap_producer_timeout_ms" | "GLOBAL" | "Bootstrap producer timeout in milliseconds" | "Integer" |
| "YBC socket read timeout" | "ybc.timeout.socket_read_timeout_ms" | "GLOBAL" | "YBC client socket read timeout in milliseconds" | "Integer" |
| "YBC operation timeout" | "ybc.timeout.operation_timeout_ms" | "GLOBAL" | "YBC client timeout in milliseconds for operations" | "Integer" |
| "Server certificate verification for S3 backup/restore" | "yb.certVerifyBackupRestore.is_enforced" | "GLOBAL" | "Enforce server certificate verification during S3 backup/restore" | "Boolean" |
| "Javax Net SSL TrustStore" | "yb.wellKnownCA.trustStore.path" | "GLOBAL" | "Java property javax.net.ssl.trustStore" | "String" |
| "Javax Net SSL TrustStore Type" | "yb.wellKnownCA.trustStore.type" | "GLOBAL" | "Java property javax.net.ssl.trustStoreType" | "String" |
| "Javax Net SSL TrustStore Password" | "yb.wellKnownCA.trustStore.password" | "GLOBAL" | "Java property javax.net.ssl.trustStorePassword" | "String" |
| "Enable Cert Reload" | "yb.features.cert_reload.enabled" | "GLOBAL" | "Enable hot reload of TLS certificates without restart of the DB nodes" | "Boolean" |
| "Organization name for self signed certificates" | "yb.tlsCertificate.organizationName" | "GLOBAL" | "Specify an organization name for self signed certificates" | "String" |
| "Delete Output File" | "yb.logs.cmdOutputDelete" | "GLOBAL" | "Flag to delete temp output file created by the shell command" | "Boolean" |
| "Shell Output Retention Duration" | "yb.logs.shell.output_retention_hours" | "GLOBAL" | "Output logs for shell commands are written to tmp folder.This setting defines how long will we wait before garbage collecting them." | "Integer" |
| "Shell Output Max Directory Size" | "yb.logs.shell.output_dir_max_size" | "GLOBAL" | "Output logs for shell commands are written to tmp folder.This setting defines rotation policy based on directory size." | "Bytes" |
| "Max Size of each log message" | "yb.logs.max_msg_size" | "GLOBAL" | "We limit the length of each log line as sometimes we dump entire output of script. If you want to debug something specific and the script output isgetting truncated in application log then increase this limit" | "Bytes" |
| "KMS Refresh Interval" | "yb.kms.refresh_interval" | "GLOBAL" | "Default refresh interval for the KMS providers." | "Duration" |
| "Server certificate verification for LDAPs/LDAP-TLS" | "yb.security.ldap.enforce_server_cert_verification" | "GLOBAL" | "Enforce server certificate verification for LDAPs/LDAP-TLS" | "Boolean" |
| "Enable Detailed Logs" | "yb.security.enable_detailed_logs" | "GLOBAL" | "Enable detailed security logs" | "Boolean" |
| "Maximum Volume Count" | "yb.max_volume_count" | "GLOBAL" | "Maximum Volume Count" | "Integer" |
| "Task Garbage Collector Check Interval" | "yb.taskGC.gc_check_interval" | "GLOBAL" | "How frequently do we check for completed tasks in database" | "Duration" |
| "API support for backward compatible date fields" | "yb.api.backward_compatible_date" | "GLOBAL" | "Enable when a client to the YBAnywhere API wants to continue using the older date  fields in non-ISO format. Default behaviour is to not populate such deprecated API fields and only return newer date fields." | "Boolean" |
| "Allow universes to be detached/attached" | "yb.attach_detach.enabled" | "GLOBAL" | "Allow universes to be detached from a source platform and attached to dest platform" | "Boolean" |
| "Whether YBA supports transactional xCluster configs" | "yb.xcluster.transactional.enabled" | "GLOBAL" | "It indicates whether YBA should support transactional xCluster configs" | "Boolean" |
| "Enable disaster recovery" | "yb.xcluster.dr.enabled" | "GLOBAL" | "It indicates whether creating disaster recovery configs are enabled" | "Boolean" |
| "Enable xcluster/DR auto flag validation" | "yb.xcluster.enable_auto_flag_validation" | "GLOBAL" | "Enables checks for xcluster/disaster recovery validations for autoflags for xcluster/DR operations" | "Boolean" |
| "Enable YBC for xCluster" | "yb.xcluster.use_ybc" | "GLOBAL" | "Enable YBC to take backup and restore during xClsuter bootstrap" | "Boolean" |
| "Whether installation of YugabyteDB version higher than YBA version is allowed" | "yb.allow_db_version_more_than_yba_version" | "GLOBAL" | "It indicates whether the installation of YugabyteDB with a version higher than YBA version is allowed on universe nodes" | "Boolean" |
| "Skip DB / YBA version comparison checks" | "yb.skip_version_checks" | "GLOBAL" | "Whether we should skip DB / YBA version comparison checks during upgrades, etc. Gives more flexibilty, but user should be careful when enabling this." | "Boolean" |
| "Path to pg_dump on the YBA node" | "db.default.pg_dump_path" | "GLOBAL" | "Set during yba-installer for both custom postgres and version specific postgres installation" | "String" |
| "Path to pg_restore on the YBA node" | "db.default.pg_restore_path" | "GLOBAL" | "Set during yba-installer for both custom postgres and version specific postgres installation" | "String" |
| "Regex for match Yugabyte DB release .tar.gz files" | "yb.regex.release_pattern.ybdb" | "GLOBAL" | "Regex pattern used to find Yugabyte DB release .tar.gz files" | "String" |
| "Regex for match Yugabyte DB release helm .tar.gz files" | "yb.regex.release_pattern.helm" | "GLOBAL" | "Regex pattern used to find Yugabyte DB helm .tar.gz files" | "String" |
| "tmp directory path" | "yb.filepaths.tmpDirectory" | "GLOBAL" | "Path to the tmp directory to be used by YBA" | "String" |
| "Whether to wait for clock skew decrease" | "yb.wait_for_clock_sync.inline_enabled" | "GLOBAL" | "Whether to wait for the clock skew to go below the threshold before starting the master and tserver processes" | "Boolean" |
| "Delete Expired Backup MAX GC Size" | "yb.backup.delete_expired_backup_max_gc_size" | "GLOBAL" | "Number of expired backups to be deleted in a single GC iteration." | "Integer" |
| "Prometheus external URL" | "yb.metrics.external.url" | "GLOBAL" | "URL used to generate Prometheus metrics on YBA UI and to set up HA metrics federation." | "String" |
| "Prometheus link use browser FQDN" | "yb.metrics.link.use_browser_fqdn" | "GLOBAL" | "If Prometheus link in browser should point to current FQDN in browser or use value from backend." | "Boolean" |
| "Devops command timeout" | "yb.devops.command_timeout" | "GLOBAL" | "Devops command timeout" | "Duration" |
| "Node destroy command timeout" | "yb.node_ops.destroy_server_timeout" | "GLOBAL" | "Timeout for node destroy command before failing." | "Duration" |
| "YBC Compatible DB Version" | "ybc.compatible_db_version" | "GLOBAL" | "Minimum YBDB version which supports YBC" | "String" |
| "Prometheus auth enabled" | "yb.metrics.auth" | "GLOBAL" | "Enables basic authentication for Prometheus web UI/APIs access" | "Boolean" |
| "Prometheus auth username" | "yb.metrics.auth_username" | "GLOBAL" | "Username, used for request authentication against embedded Prometheus" | "String" |
| "Prometheus auth password" | "yb.metrics.auth_password" | "GLOBAL" | "Password, used for request authentication against embedded Prometheus" | "String" |
| "Force YBC Shutdown during upgrade" | "ybc.upgrade.force_shutdown" | "GLOBAL" | "For YBC Shutdown during upgrade" | "Boolean" |
| "Enable strict mode to ignore deprecated YBA APIs" | "yb.api.mode.strict" | "GLOBAL" | "Will ignore deprecated APIs" | "Boolean" |
| "Enable safe mode to ignore preview YBA APIs" | "yb.api.mode.safe" | "GLOBAL" | "Will ignore preview APIs" | "Boolean" |
| "Enable publishing thread dumps to GCS" | "yb.diag.thread_dumps.gcs.enabled" | "GLOBAL" | "Enable publishing thread dumps to GCS" | "Boolean" |
| "Operator owned resources api block" | "yb.kubernetes.operator.block_api_operator_owned_resources" | "GLOBAL" | "A resource controlled by the kubernetes operator cannot be updated using the REST API" | "Boolean" |
| "Granular level metrics" | "yb.ui.feature_flags.granular_metrics" | "GLOBAL" | "View granular level metrics when user selects specific time period in a chart" | "Boolean" |
| "Enable multiline option for GFlag conf." | "yb.ui.feature_flags.gflag_multiline_conf" | "GLOBAL" | "Allows user to enter postgres hba rules and ident map rules in multiple rows" | "Boolean" |
| "Disable hostname cert validation for HA communication" | "yb.ha.ws.ssl.loose.disableHostnameVerification" | "GLOBAL" | "When set, the hostname in https certs will not be validated for HA communication. Communication will still be encrypted." | "Boolean" |
| "HA test connection request timeout" | "yb.ha.test_request_timeout" | "GLOBAL" | "The request to test HA connection to standby will timeout after the specified amount of time." | "Duration" |
| "HA test connection connection timeout" | "yb.ha.test_connection_timeout" | "GLOBAL" | "The client will wait for the specified amount of time to make a connection to the remote address." | "Duration" |
| "XCluster isBootstrapRequired rpc max parallel threads" | "yb.xcluster.is_bootstrap_required_rpc_pool.max_threads" | "GLOBAL" | "Sets the maximum allowed number of threads to be run concurrently for xcluster isBootstrapRequired rpc" | "Integer" |
| "Auto create user on SSO login" | "yb.security.oidc_enable_auto_create_users" | "GLOBAL" | "Enable user creation on SSO login" | "Boolean" |
| "YBC poll upgrade result tries" | "ybc.upgrade.poll_result_tries" | "GLOBAL" | "YBC poll upgrade result tries count." | "Integer" |
| "YBC poll upgrade result Sleep time" | "ybc.upgrade.poll_result_sleep_ms" | "GLOBAL" | "YBC poll upgrade result sleep time." | "Long" |
| "HA Shutdown Level" | "yb.ha.shutdown_level" | "GLOBAL" | "When to shutdown - 0 for never, 1 for promotion, 2 for promotion and demotion" | "Integer" |
| "OIDC Refresh Access Token Interval" | "yb.security.oidcRefreshTokenInterval" | "GLOBAL" | "If configured, YBA will refresh the access token at the specified duration, defaulted to 5 minutes." | "Duration" |
| "Allow Editing of in-use Linux Versions" | "yb.edit_provider.new.allow_used_bundle_edit" | "GLOBAL" | "Caution: If enabled, YBA will blindly allow editing the name/AMI associated with the bundle, without propagating it to the in-use Universes" | "Boolean" |
| "Clock Skew" | "yb.alert.max_clock_skew_ms" | "UNIVERSE" | "Default threshold for Clock Skew alert" | "Duration" |
| "Health Log Output" | "yb.health.logOutput" | "UNIVERSE" | "It determines whether to log the output of the node health check script to the console" | "Boolean" |
| "Node Checkout Time" | "yb.health.nodeCheckTimeoutSec" | "UNIVERSE" | "The timeout (in seconds) for node check operation as part of universe health check" | "Integer" |
| "YB Upgrade Blacklist Leaders" | "yb.upgrade.blacklist_leaders" | "UNIVERSE" | "Determines (boolean) whether we enable/disable leader blacklisting when performing universe/node tasks" | "Boolean" |
| "YB Upgrade Blacklist Leader Wait Time in Ms" | "yb.upgrade.blacklist_leader_wait_time_ms" | "UNIVERSE" | "The timeout (in milliseconds) that we wait of leader blacklisting on a node to complete" | "Integer" |
| "Fail task on leader blacklist timeout" | "yb.node_ops.leader_blacklist.fail_on_timeout" | "UNIVERSE" | "Determines (boolean) whether we fail the task after waiting for leader blacklist timeout is reached" | "Boolean" |
| "YB Upgrade Max Follower Lag Threshold " | "yb.upgrade.max_follower_lag_threshold_ms" | "UNIVERSE" | "The maximum time (in milliseconds) that we allow a tserver to be behind its peers" | "Integer" |
| "YB Upgrade Use Single Connection Param" | "yb.upgrade.single_connection_ysql_upgrade" | "UNIVERSE" | "The flag, which controls, if YSQL catalog upgrade will be performed in single or multi connection mode.Single connection mode makes it work even on tiny DB nodes." | "Boolean" |
| "YB edit sleep time in ms before blacklist clear in ms" | "yb.edit.wait_before_blacklist_clear" | "UNIVERSE" | "Sleep time before clearing nodes from blacklist in ms" | "Duration" |
| "Default Releases Count" | "yb.releases.num_releases_to_keep_default" | "UNIVERSE" | "Number of Releases to Keep" | "Integer" |
| "Cloud Releases Count" | "yb.releases.num_releases_to_keep_cloud" | "UNIVERSE" | "Number Of Cloud Releases To Keep" | "Integer" |
| "DB Available Mem Limit" | "yb.dbmem.checks.mem_available_limit_kb" | "UNIVERSE" | "Minimum available memory required on DB nodes for software upgrade." | "Long" |
| "PG Based Backup" | "yb.backup.pg_based" | "UNIVERSE" | "Enable PG-based backup" | "Boolean" |
| "DB Read Write Test" | "yb.metrics.db_read_write_test" | "UNIVERSE" | "The flag defines, if we perform DB write-read check on DB nodes or not." | "Boolean" |
| "YSQLSH Connectivity Test" | "yb.metrics.ysqlsh_connectivity_test" | "UNIVERSE" | "The flag defines, if we perform YSQLSH Connectivity check on DB nodes or not." | "Boolean" |
| "CQLSH Connectivity Test" | "yb.metrics.cqlsh_connectivity_test" | "UNIVERSE" | "The flag defines, if we perform CQLSH Connectivity check on DB nodes or not." | "Boolean" |
| "Metrics Collection Level" | "yb.metrics.collection_level" | "UNIVERSE" | "DB node metrics collection level.ALL - collect all metrics, NORMAL - default value, which only limits some per-table metrics, MINIMAL - limits both node level and further limits table level metrics we collect and OFF to completely disable metric collection." | "String" |
| "Universe Version Check Mode" | "yb.universe_version_check_mode" | "UNIVERSE" | "Possible values: NEVER, HA_ONLY, ALWAYS" | "VersionCheckMode" |
| "Override Force Universe Lock" | "yb.task.override_force_universe_lock" | "UNIVERSE" | "Whether overriding universe lock is allowed when force option is selected.If it is disabled, force option will wait for the lock to be released." | "Boolean" |
| "Enable SSH Key Expiration" | "yb.security.ssh_keys.enable_ssh_key_expiration" | "UNIVERSE" | "TODO" | "Boolean" |
| "SSh Key Expiration Threshold" | "yb.security.ssh_keys.ssh_key_expiration_threshold_days" | "UNIVERSE" | "TODO" | "Integer" |
| "Enable SSE" | "yb.backup.enable_sse" | "UNIVERSE" | "Enable SSE during backup/restore" | "Boolean" |
| "Allow Table by Table backups for YCQL" | "yb.backup.allow_table_by_table_backup_ycql" | "UNIVERSE" | "Backup tables individually during YCQL backup" | "Boolean" |
| "NFS Directry Path" | "yb.ybc_flags.nfs_dirs" | "UNIVERSE" | "Authorised NFS directories for backups" | "String" |
| "Enable Verbose Logging" | "yb.ybc_flags.enable_verbose" | "UNIVERSE" | "Enable verbose ybc logging" | "Boolean" |
| "Max Thread Count" | "yb.perf_advisor.max_threads" | "UNIVERSE" | "Max number of threads to support parallel querying of nodes" | "Integer" |
| "Allow Scheduled YBC Upgrades" | "ybc.upgrade.allow_scheduled_upgrade" | "UNIVERSE" | "Enable Scheduled upgrade of ybc on the universe" | "Boolean" |
| "Allow User Gflags Override" | "yb.gflags.allow_user_override" | "UNIVERSE" | "Allow users to override default Gflags values" | "Boolean" |
| "Enable Trigger API" | "yb.health.trigger_api.enabled" | "UNIVERSE" | "Allow trigger_health_check API to be called" | "Boolean" |
| "Verbose Backup Log" | "yb.backup.log.verbose" | "UNIVERSE" | "Enable verbose backup logging" | "Boolean" |
| "Wait for LB for Added Nodes" | "yb.wait_for_lb_for_added_nodes" | "UNIVERSE" | "Wait for Load Balancer for added nodes" | "Boolean" |
| "Wait For master Leader timeout" | "yb.wait_for_master_leader_timeout" | "UNIVERSE" | "Time in seconds to wait for master leader before timeout for List tables API" | "Duration" |
| "Slow Queries Limit" | "yb.query_stats.slow_queries.limit" | "UNIVERSE" | "The number of queries to fetch." | "Integer" |
| "Slow Queries Order By Key" | "yb.query_stats.slow_queries.order_by" | "UNIVERSE" | "We sort queries by this metric. Possible values: total_time, max_time, mean_time, rows, calls" | "String" |
| "Turn off batch nest loop for running slow sql queries" | "yb.query_stats.slow_queries.set_enable_nestloop_off" | "UNIVERSE" | "This config turns off and on batch nestloop during running the join statement for slow queries. If true, it will be turned off and we expect better performance." | "Boolean" |
| "Excluded Queries" | "yb.query_stats.excluded_queries" | "UNIVERSE" | "List of queries to exclude from slow queries." | "String List" |
| "Query character limit" | "yb.query_stats.slow_queries.query_length" | "UNIVERSE" | "Query character limit in slow queries." | "Integer" |
| "Ansible Strategy" | "yb.ansible.strategy" | "UNIVERSE" | "strategy can be linear, mitogen_linear or debug" | "String" |
| "Ansible Connection Timeout Duration" | "yb.ansible.conn_timeout_secs" | "UNIVERSE" | "This is the default timeout for connection plugins to use." | "Integer" |
| "Ansible Verbosity Level" | "yb.ansible.verbosity" | "UNIVERSE" | "verbosity of ansible logs, 0 to 4 (more verbose)" | "Integer" |
| "Ansible Debug Output" | "yb.ansible.debug" | "UNIVERSE" | "Debug output (can include secrets in output)" | "Boolean" |
| "Ansible Diff Always" | "yb.ansible.diff_always" | "UNIVERSE" | "Configuration toggle to tell modules to show differences when in 'changed' status, equivalent to --diff." | "Boolean" |
| "Ansible Local Temp Directory" | "yb.ansible.local_temp" | "UNIVERSE" | "Temporary directory for Ansible to use on the controller." | "String" |
| "Enable Performance Advisor" | "yb.perf_advisor.enabled" | "UNIVERSE" | "Defines if performance advisor is enabled for the universe or not" | "Boolean" |
| "Performance Advisor Run Frequency" | "yb.perf_advisor.universe_frequency_mins" | "UNIVERSE" | "Defines performance advisor run frequency for universe" | "Integer" |
| "Performance Advisor connection skew threshold" | "yb.perf_advisor.connection_skew_threshold_pct" | "UNIVERSE" | "Defines max difference between avg connections count usage and node connection count before connection skew recommendation is raised" | "Double" |
| "Performance Advisor connection skew min connections" | "yb.perf_advisor.connection_skew_min_connections" | "UNIVERSE" | "Defines minimal number of connections for connection skew recommendation to be raised" | "Integer" |
| "Performance Advisor connection skew interval mins" | "yb.perf_advisor.connection_skew_interval_mins" | "UNIVERSE" | "Defines time interval for connection skew recommendation check, in minutes" | "Integer" |
| "Performance Advisor cpu skew threshold" | "yb.perf_advisor.cpu_skew_threshold_pct" | "UNIVERSE" | "Defines max difference between avg cpu usage and node cpu usage before cpu skew recommendation is raised" | "Double" |
| "Performance Advisor cpu skew min usage" | "yb.perf_advisor.cpu_skew_min_usage_pct" | "UNIVERSE" | "Defines minimal cpu usage for cpu skew recommendation to be raised" | "Double" |
| "Performance Advisor cpu skew interval mins" | "yb.perf_advisor.cpu_skew_interval_mins" | "UNIVERSE" | "Defines time interval for cpu skew recommendation check, in minutes" | "Integer" |
| "Performance Advisor CPU usage threshold" | "yb.perf_advisor.cpu_usage_threshold" | "UNIVERSE" | "Defines max allowed average CPU usage per 10 minutes before CPU usage recommendation is raised" | "Double" |
| "Performance Advisor cpu usage interval mins" | "yb.perf_advisor.cpu_usage_interval_mins" | "UNIVERSE" | "Defines time interval for cpu usage recommendation check, in minutes" | "Integer" |
| "Performance Advisor query skew threshold" | "yb.perf_advisor.query_skew_threshold_pct" | "UNIVERSE" | "Defines max difference between avg queries count and node queries count before query skew recommendation is raised" | "Double" |
| "Performance Advisor query skew min queries" | "yb.perf_advisor.query_skew_min_queries" | "UNIVERSE" | "Defines minimal queries count for query skew recommendation to be raised" | "Integer" |
| "Performance Advisor query skew interval mins" | "yb.perf_advisor.query_skew_interval_mins" | "UNIVERSE" | "Defines time interval for query skew recommendation check, in minutes" | "Integer" |
| "Performance Advisor rejected connections threshold" | "yb.perf_advisor.rejected_conn_threshold" | "UNIVERSE" | "Defines number of rejected connections during configured interval for rejected connections recommendation to be raised " | "Integer" |
| "Performance Advisor rejected connections interval mins" | "yb.perf_advisor.rejected_conn_interval_mins" | "UNIVERSE" | "Defines time interval for rejected connections recommendation check, in minutes" | "Integer" |
| "Performance Advisor hot shard write skew threshold" | "yb.perf_advisor.hot_shard_write_skew_threshold_pct" | "UNIVERSE" | "Defines max difference between average node writes and hot shard node writes before hot shard recommendation is raised" | "Double" |
| "Performance Advisor hot shard read skew threshold" | "yb.perf_advisor.hot_shard_read_skew_threshold_pct" | "UNIVERSE" | "Defines max difference between average node reads and hot shard node reads before hot shard recommendation is raised" | "Double" |
| "Performance Advisor hot shard interval mins" | "yb.perf_advisor.hot_shard_interval_mins" | "UNIVERSE" | "Defines time interval for hot hard recommendation check, in minutes" | "Integer" |
| "Performance Advisor hot shard minimal writes" | "yb.perf_advisor.hot_shard_min_node_writes" | "UNIVERSE" | "Defines min writes for hot shard recommendation to be raised" | "Integer" |
| "Performance Advisor hot shard minimal reads" | "yb.perf_advisor.hot_shard_min_node_reads" | "UNIVERSE" | "Defines min reads for hot shard recommendation to be raised" | "Integer" |
| "Skip TLS Cert Validation" | "yb.tls.skip_cert_validation" | "UNIVERSE" | "Used to skip certificates validation for the configure phase.Possible values - ALL, HOSTNAME, NONE" | "SkipCertValidationType" |
| "Clean Orphan snapshots" | "yb.snapshot_cleanup.delete_orphan_on_startup" | "UNIVERSE" | "Clean orphan(non-scheduled) snapshots on Yugaware startup/restart" | "Boolean" |
| "Enable https on Master/TServer UI" | "yb.node_ui.https.enabled" | "UNIVERSE" | "Allow https on Master/TServer UI for a universe" | "Boolean" |
| "Helm Timeout in Seconds" | "yb.helm.timeout_secs" | "UNIVERSE" | "Timeout used for internal universe-level helm operations like install/upgrade in secs" | "Long" |
| "Enable Perf Advisor to view recommendations" | "yb.ui.feature_flags.perf_advisor" | "UNIVERSE" | "Builds recommendations to help tune our applications accordingly" | "Boolean" |
| "Promote AutoFlags" | "yb.upgrade.promote_auto_flag" | "UNIVERSE" | "Promotes Auto flags while upgrading YB-DB" | "Boolean" |
| "Sleep time after auto flags are updated." | "yb.upgrade.auto_flag_update_sleep_time_ms" | "UNIVERSE" | "Controls the amount of time(ms) to wait after auto flags are updated" | "Duration" |
| "Allow upgrade on transit universe" | "yb.upgrade.allow_upgrade_on_transit_universe" | "UNIVERSE" | "Allow universe upgrade when nodes are in transit mode" | "Boolean" |
| "Promote AutoFlags Forcefully" | "yb.upgrade.promote_flags_forcefully" | "UNIVERSE" | "Promote AutoFlags Forcefully during software upgrade" | "Boolean" |
| "Minimum Incremental backup schedule frequency" | "yb.backup.minIncrementalScheduleFrequencyInSecs" | "UNIVERSE" | "Minimum Incremental backup schedule frequency in seconds" | "Long" |
| "Universe logs regex pattern" | "yb.support_bundle.universe_logs_regex_pattern" | "UNIVERSE" | "Universe logs regex pattern in support bundle" | "String" |
| "Postgres logs regex pattern" | "yb.support_bundle.postgres_logs_regex_pattern" | "UNIVERSE" | "Postgres logs regex pattern in support bundle" | "String" |
| "YSQL Upgrade Timeout in seconds" | "yb.upgrade.ysql_upgrade_timeout_sec" | "UNIVERSE" | "Controls the yb-client admin operation timeout when performing the runUpgradeYSQL subtask rpc calls." | "Integer" |
| "Under replicated tablets check timeout" | "yb.checks.under_replicated_tablets.timeout" | "UNIVERSE" | "Controls the max time out when performing the checkUnderReplicatedTablets subtask" | "Duration" |
| "Enabling under replicated tablets check" | "yb.checks.under_replicated_tablets.enabled" | "UNIVERSE" | "Controls whether or not to perform the checkUnderReplicatedTablets subtask" | "Boolean" |
| "Master config change result check timeout" | "yb.checks.change_master_config.timeout" | "UNIVERSE" | "Controls the max time out when waiting for master config change to finish" | "Duration" |
| "Enabling Master config change result check" | "yb.checks.change_master_config.enabled" | "UNIVERSE" | "Controls whether or not to wait for master config change to finish" | "Boolean" |
| "Enabling follower lag check" | "yb.checks.follower_lag.enabled" | "UNIVERSE" | "Controls whether or not to perform the follower lag checks" | "Boolean" |
| "Follower lag check timeout" | "yb.checks.follower_lag.timeout" | "UNIVERSE" | "Controls the max time out when performing follower lag checks" | "Duration" |
| "Max threshold for follower lag" | "yb.checks.follower_lag.max_threshold" | "UNIVERSE" | "The maximum time that we allow a tserver to be behind its peers" | "Duration" |
| "Wait for server ready timeout" | "yb.checks.wait_for_server_ready.timeout" | "UNIVERSE" | "Controls the max time for server to finish locally bootstrapping" | "Duration" |
| "Memory check timeout" | "yb.dbmem.checks.timeout" | "UNIVERSE" | "Timeout for memory check in secs" | "Long" |
| "Wait time before doing restore during xCluster setup task" | "yb.xcluster.sleep_time_before_restore" | "UNIVERSE" | "The amount of time to sleep (wait) before executing restore subtask during xCluster setup; it is useful because xCluster setup also drops the database before restore and the sleep makes sure the drop operation has reached all the nodes" | "Duration" |
| "Use server broadcast address for yb_backup" | "yb.backup.use_server_broadcast_address_for_yb_backup" | "UNIVERSE" | "Controls whether server_broadcast_address entry should be used during yb_backup.py backup/restore" | "Boolean" |
| "Slow Queries Timeout" | "yb.query_stats.slow_queries.timeout_secs" | "UNIVERSE" | "Timeout in secs for slow queries" | "Long" |
| "YSQL Queries Timeout" | "yb.ysql_timeout_secs" | "UNIVERSE" | "Timeout in secs for YSQL queries" | "Long" |
| "Number of cores to keep" | "yb.num_cores_to_keep" | "UNIVERSE" | "Controls the configuration to set the number of cores to keep in the Ansible layer" | "Integer" |
| "Whether to check YBA xCluster object is in sync with DB replication group" | "yb.xcluster.ensure_sync_get_replication_status" | "UNIVERSE" | "It ensures that the YBA XCluster object for tables that are in replication is in sync with replication group in DB. If they are not in sync and this is true, getting the xCluster object will throw an exception and the user has to resync the xCluster config." | "Boolean" |
| "Network Load balancer health check ports" | "yb.universe.network_load_balancer.custom_health_check_ports" | "UNIVERSE" | "Ports to use for health checks performed by the network load balancer. Invalid and duplicate ports will be ignored. For GCP, only the first health check port would be used." | "Integer List" |
| "Network Load balancer health check protocol" | "yb.universe.network_load_balancer.custom_health_check_protocol" | "UNIVERSE" | "Protocol to use for health checks performed by the network load balancer" | "Protocol" |
| "Network Load balancer health check paths" | "yb.universe.network_load_balancer.custom_health_check_paths" | "UNIVERSE" | "Paths probed by HTTP/HTTPS health checks performed by the network load balancer. Paths are mapped one-to-one with the custom health check ports runtime configuration." | "String List" |
| "Validate filepath for local release" | "yb.universe.validate_local_release" | "UNIVERSE" | "For certain tasks validates the existence of local filepath for the universe software version." | "Boolean" |
| "Default PITR retention period for txn xCluster" | "yb.xcluster.transactional.pitr.default_retention_period" | "UNIVERSE" | "The default retention period used to create PITR configs for transactional xCluster replication; it will be used when there is no existing PITR configs and it is not specified in the task parameters" | "Duration" |
| "Default PITR snapshot interval for txn xCluster" | "yb.xcluster.transactional.pitr.default_snapshot_interval" | "UNIVERSE" | "The default snapshot interval used to create PITR configs for transactional xCluster replication; it will be used when there is no existing PITR configs and it is not specified in the task parameters" | "Duration" |
| "Skip backup metadata validation" | "yb.backup.skip_metadata_validation" | "UNIVERSE" | "Skip backup metadata based validation during restore" | "Boolean" |
| "Parallelism for Node Agent Reinstallation" | "yb.node_agent.reinstall_parallelism" | "UNIVERSE" | "Number of parallel node agent reinstallations at a time" | "Integer" |
| "To perform sync of user-groups between the Universe DB nodes and LDAP Server" | "yb.security.ldap.ldap_universe_sync" | "UNIVERSE" | "If configured, this feature allows users to synchronise user groups configured on the upstream LDAP Server with user roles in YBDB nodes associated with the universe." | "Boolean" |
| "Cluster membership check timeout" | "yb.checks.cluster_membership.timeout" | "UNIVERSE" | "Controls the max time to check that there are no tablets assigned to the node" | "Duration" |
| "Wait time for xcluster/DR replication setup and edit RPCs." | "yb.xcluster.operation_timeout" | "UNIVERSE" | "Wait time for xcluster/DR replication setup and edit RPCs." | "Duration" |
| "Leaderless tablets check enabled" | "yb.checks.leaderless_tablets.enabled" | "UNIVERSE" | " Whether to run CheckLeaderlessTablets subtask before running universe tasks" | "Boolean" |
| "Leaderless tablets check timeout" | "yb.checks.leaderless_tablets.timeout" | "UNIVERSE" | "Controls the max time out when performing the CheckLeaderlessTablets subtask" | "Duration" |
| "Enable Clock Sync check" | "yb.wait_for_clock_sync.enabled" | "UNIVERSE" | "Enable Clock Sync check" | "Boolean" |
| "Enable YBC" | "ybc.universe.enabled" | "UNIVERSE" | "Enable YBC for universes during software upgrade" | "Boolean" |
| "Target Node Disk Usage Percentage" | "yb.checks.node_disk_size.target_usage_percentage" | "UNIVERSE" | "Percentage of current disk usage that may consume on the target nodes" | "Integer" |
