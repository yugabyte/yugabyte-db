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
| "Enable downloading metrics as a PDF" | "yb.ui.metrics.enable_download_pdf" | "CUSTOMER" | "When enabled, the download metrics option is shown on the universe metrics page." | "Boolean" |
| "Use Redesigned Provider UI" | "yb.ui.feature_flags.provider_redesign" | "CUSTOMER" | "The redesigned provider UI adds a provider list view, a provider details view and improves the provider creation form for AWS, AZU, GCP, and K8s" | "Boolean" |
| "Enable partial editing of in use providers" | "yb.ui.feature_flags.edit_in_use_provider" | "CUSTOMER" | "A subset of fields from in use providers can be edited. Users can edit in use providers directly through the YBA API. This config is used to enable this functionality through YBA UI as well." | "Boolean" |
| "Show underlying xCluster configs from DR setup" | "yb.ui.xcluster.dr.show_xcluster_config" | "CUSTOMER" | "YBA creates an underlying transactional xCluster config when setting up an active-active single-master disaster recovery (DR) config. During regular operation you should manage the DR config through the DR UI instead of the xCluster UI. This feature flag serves as a way to expose the underlying xCluster config for troubleshooting." | "Boolean" |
| "Enable the option to skip creating a full copy for xCluster operations" | "yb.ui.xcluster.enable_skip_bootstrapping" | "CUSTOMER" | "Enabling this runtime config will expose an option in the create xCluster modal and select tables modal to skip creating a full copy for xCluster replication configs." | "Boolean" |
| "Enforce User Tags" | "yb.universe.user_tags.is_enforced" | "CUSTOMER" | "Prevents universe creation when the enforced tags are not provided." | "Boolean" |
| "Enforced User Tags List" | "yb.universe.user_tags.enforced_tags" | "CUSTOMER" | "A list of enforced user tag and accepted value pairs during universe creation. Pass '*' to accept all values for a tag. Ex: [\"yb_task:dev\",\"yb_task:test\",\"yb_owner:*\",\"yb_dept:eng\",\"yb_dept:qa\", \"yb_dept:product\", \"yb_dept:sales\"]" | "Key Value SetMultimap" |
| "Enable IMDSv2" | "yb.aws.enable_imdsv2_support" | "CUSTOMER" | "Enable IMDSv2 support for AWS providers" | "Boolean" |
| "Backup Garbage Collector Number of Retries" | "yb.backupGC.number_of_retries" | "CUSTOMER" | "Number of retries during backup deletion" | "Integer" |
| "Enable Certificate Config Validation" | "yb.tls.enable_config_validation" | "CUSTOMER" | "Certificate configuration validation during the addition of new certificates." | "Boolean" |
| "Default Metric Graph Point Count" | "yb.metrics.default_points" | "CUSTOMER" | "Default Metric Graph Point Count, if step is not defined in the query" | "Integer" |
| "Fetch Batch Size of Task Info" | "yb.task_info_db_query_batch_size" | "CUSTOMER" | "Knob that can be used to make lesser number of calls to DB" | "Integer" |
| "Use Ansible for provisioning" | "yb.node_agent.use_ansible_provisioning" | "CUSTOMER" | "If enabled use Ansible for provisioning" | "Boolean" |
| "Notify user on password reset" | "yb.user.send_password_reset_notification" | "CUSTOMER" | "If enabled, user will be notified on password reset" | "Boolean" |
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
| "Enable Ansible Offloading" | "yb.node_agent.ansible_offloading.enabled" | "PROVIDER" | "Offload ansible tasks to the DB nodes." | "Boolean" |
| "Remote tmp directory" | "yb.filepaths.remoteTmpDirectory" | "PROVIDER" | "A remote temporary directory should be used for performing operations on nodes within the provider scope." | "String" |
| "Polling interval for GCP Opertion status" | "yb.gcp.operations.status_polling_interval" | "PROVIDER" | "Interval to poll the status of an ongoing GCP resource creation operation." | "Duration" |
| "GCP Operation Timeout interval" | "yb.gcp.operations.timeout_interval" | "PROVIDER" | "Timeout interval to wait for GCP resource creation operations to complete sucessfully." | "Duration" |
| "Make YBC listen on 0.0.0.0" | "yb.ybc_flags.listen_on_all_interfaces_k8s" | "PROVIDER" | "Makes YBC bind on all network interfaces" | "Boolean" |
| "Azure Virtual Machine Params blob" | "yb.azure.custom_params.vm" | "PROVIDER" | "Custom JSON of Azure parameters to apply on top of virtual machine creation." | "String" |
| "Azure Disk Params blob" | "yb.azure.custom_params.disk" | "PROVIDER" | "Custom JSON of Azure parameters to apply on top of data disk creation." | "String" |
| "Azure Network Interface Params blob" | "yb.azure.custom_params.network" | "PROVIDER" | "Custom JSON of Azure parameters to apply on top of network interface creation." | "String" |
| "Ignore VM plan information" | "yb.azure.vm.ignore_plan" | "PROVIDER" | "Skip passing in any plan information when creating virtual machine, even if found." | "Boolean" |
| "Show Premium V2 storage type" | "yb.azure.show_premiumv2_storage_type" | "PROVIDER" | "Show Premium V2 storage type during create/edit universe flow." | "Boolean" |
| "Monitored mount roots" | "yb.provider.monitored_mount_roots" | "PROVIDER" | "Mount roots, which we show on the merics dashboard and which we're alerting on." | "String" |
| "Enable Geo-partitioning" | "yb.universe.geo_partitioning_enabled" | "PROVIDER" | "Enables geo-partitioning for universes created with this provider." | "Boolean" |
| "Enable YBC" | "ybc.provider.enabled" | "PROVIDER" | "Enable YBC for universes created with this provider" | "Boolean" |
| "Configure OpenTelemetry metrics port" | "yb.universe.otel_collector_metrics_port" | "PROVIDER" | "OpenTelemetry metrics port" | "Integer" |
| "Default service scope for K8s universe" | "yb.universe.default_service_scope_for_k8s" | "PROVIDER" | "The default service scope for K8s service endpoints. Can be AZ/Namespaced. 'AZ' will create a service in each Availability zone, whereas 'Namespaced' will create one service per Namespace" | "String" |
| "Show Hyperdisk storage types" | "yb.gcp.show_hyperdisks_storage_type" | "PROVIDER" | "Show Hyperdisk storage types during create/edit universe flow." | "Boolean" |
| "Configure Clockbound when using cloud providers" | "yb.provider.configure_clockbound_cloud_provisioning" | "PROVIDER" | "Configure clockbound when creating cloud provider based Universes" | "Boolean" |
| "GCP Connection Draining Timeout" | "yb.gcp.operations.connection_draining_timeout" | "PROVIDER" | "Set the connection draining timeout for the GCP load balancer." | "Duration" |
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
| "Application Logs Regex Pattern" | "yb.support_bundle.application_logs_regex_pattern" | "GLOBAL" | "Regex pattern used to filter application log files when creating support bundles." | "String" |
| "Snapshot creation max attempts" | "yb.snapshot_creation.max_attempts" | "GLOBAL" | "Max attempts while waiting for AWS Snapshot Creation" | "Integer" |
| "Snapshot creation delay" | "yb.snapshot_creation.delay" | "GLOBAL" | "Delay per attempt while waiting for AWS Snapshot Creation" | "Integer" |
| "Runtime Config UI" | "yb.runtime_conf_ui.enable_for_all" | "GLOBAL" | "Allows users to view the runtime configuration properties via UI" | "Boolean" |
| "YBC Upgrade Interval" | "ybc.upgrade.scheduler_interval" | "GLOBAL" | "YBC Upgrade interval" | "Duration" |
| "YBC Universe Upgrade Batch Size" | "ybc.upgrade.universe_batch_size" | "GLOBAL" | "The number of maximum universes on which ybc will be upgraded simultaneously" | "Integer" |
| "YBC Node Upgrade Batch Size" | "ybc.upgrade.node_batch_size" | "GLOBAL" | "The number of maximum nodes on which ybc will be upgraded simultaneously" | "Integer" |
| "YBC Stable Release" | "ybc.releases.stable_version" | "GLOBAL" | "Stable version for Yb-Controller" | "String" |
| "YBC admin operation timeout" | "ybc.timeout.admin_operation_timeout_ms" | "GLOBAL" | "YBC client timeout in milliseconds for admin operations" | "Integer" |
| "XCluster config DB sync timeout" | "yb.xcluster.db_sync_timeout_ms" | "GLOBAL" | "XCluster config background DB sync timeout in milliseconds" | "Integer" |
| "XCluster/DR config GET API timeout" | "yb.xcluster.get_api_timeout_ms" | "GLOBAL" | "XCluster/DR config GET API timeout in milliseconds" | "Integer" |
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
| "Allow CipherTrust KMS" | "yb.kms.allow_ciphertrust" | "GLOBAL" | "Allow the usage of CipherTrust KMS." | "Boolean" |
| "Skip connectivity validations while creating Telemetry Provider" | "yb.telemetry.skip_connectivity_validations" | "GLOBAL" | "Skip connectivity and permission validations while creating Telemetry Provider." | "Boolean" |
| "Percentage of Hashicorp vault TTL to renew the token after" | "yb.kms.hcv_token_renew_percent" | "GLOBAL" | "HashiCorp Vault tokens expire when their TTL is reached. This setting renews the token after it has used the specified percentage of its original TTL. Default: 70%." | "Integer" |
| "Start Master On Stop Node" | "yb.start_master_on_stop_node" | "GLOBAL" | "Auto-start master process on a similar available node on stopping a master node" | "Boolean" |
| "Start Master On Remove Node" | "yb.start_master_on_remove_node" | "GLOBAL" | "Auto-start master process on a similar available node on removal of a master node" | "Boolean" |
| "Server certificate verification for LDAPs/LDAP-TLS" | "yb.security.ldap.enforce_server_cert_verification" | "GLOBAL" | "Enforce server certificate verification for LDAPs/LDAP-TLS" | "Boolean" |
| "Pagination query size for LDAP server" | "yb.security.ldap.page_query_size" | "GLOBAL" | "Pagination query size for LDAP server" | "Integer" |
| "Enable Detailed Logs" | "yb.security.enable_detailed_logs" | "GLOBAL" | "Enable detailed security logs" | "Boolean" |
| "Maximum Volume Count" | "yb.max_volume_count" | "GLOBAL" | "Maximum Volume Count" | "Integer" |
| "Task Garbage Collector Check Interval" | "yb.taskGC.gc_check_interval" | "GLOBAL" | "How frequently do we check for completed tasks in database" | "Duration" |
| "API support for backward compatible date fields" | "yb.api.backward_compatible_date" | "GLOBAL" | "Enable when a client to the YBAnywhere API wants to continue using the older date  fields in non-ISO format. Default behaviour is to not populate such deprecated API fields and only return newer date fields." | "Boolean" |
| "Allow universes to be detached/attached" | "yb.attach_detach.enabled" | "GLOBAL" | "Allow universes to be detached from a source platform and attached to dest platform" | "Boolean" |
| "Allow auto-provider K8s universes to attach to K8s-based YBA" | "yb.attach_detach.allow_auto_provider_to_k8s_platform" | "GLOBAL" | "Allow Kubernetes auto-provider universes to be attached to Kubernetes-based YBA. Note that you must only attach auto-provider universe to Kubernetes-based YBA if the destination and source YBA exist on the same Kubernetes cluster" | "Boolean" |
| "Whether YBA supports transactional xCluster configs" | "yb.xcluster.transactional.enabled" | "GLOBAL" | "It indicates whether YBA should support transactional xCluster configs" | "Boolean" |
| "Enable disaster recovery" | "yb.xcluster.dr.enabled" | "GLOBAL" | "It indicates whether creating disaster recovery configs are enabled" | "Boolean" |
| "Enable xcluster/DR auto flag validation" | "yb.xcluster.enable_auto_flag_validation" | "GLOBAL" | "Enables checks for xcluster/disaster recovery validations for autoflags for xcluster/DR operations" | "Boolean" |
| "Whether to log information about gathering table statuses in xCluster" | "yb.xcluster.table_status_logging_enabled" | "GLOBAL" | "Whether to log information about gathering bad table statuses in xCluster; the logs can be huge and this gives you a leverage to disable it" | "Boolean" |
| "Enable YBC for xCluster" | "yb.xcluster.use_ybc" | "GLOBAL" | "Enable YBC to take backup and restore during xClsuter bootstrap" | "Boolean" |
| "Whether installation of YugabyteDB version higher than YBA version is allowed" | "yb.allow_db_version_more_than_yba_version" | "GLOBAL" | "It indicates whether the installation of YugabyteDB with a version higher than YBA version is allowed on universe nodes" | "Boolean" |
| "Skip DB / YBA version comparison checks" | "yb.skip_version_checks" | "GLOBAL" | "Whether we should skip DB / YBA version comparison checks during upgrades, etc. Gives more flexibilty, but user should be careful when enabling this." | "Boolean" |
| "Path to pg_dump on the YBA node" | "db.default.pg_dump_path" | "GLOBAL" | "Set during yba-installer for both custom postgres and version specific postgres installation" | "String" |
| "Path to pg_restore on the YBA node" | "db.default.pg_restore_path" | "GLOBAL" | "Set during yba-installer for both custom postgres and version specific postgres installation" | "String" |
| "Regex for match Yugabyte DB release .tar.gz files" | "yb.regex.release_pattern.ybdb" | "GLOBAL" | "Regex pattern used to find Yugabyte DB release .tar.gz files" | "String" |
| "Regex for match Yugabyte DB release helm .tar.gz files" | "yb.regex.release_pattern.helm" | "GLOBAL" | "Regex pattern used to find Yugabyte DB helm .tar.gz files" | "String" |
| "Enables extra logging" | "yb.logging.enable_task_failed_request_logs" | "GLOBAL" | "Enables extra logging for task params and request body" | "Boolean" |
| "tmp directory path" | "yb.filepaths.tmpDirectory" | "GLOBAL" | "Path to the tmp directory to be used by YBA" | "String" |
| "Customer UUID to use with Kubernentes Operator" | "yb.kubernetes.operator.customer_uuid" | "GLOBAL" | "Customer UUID to use with Kubernentes Operator, do not change once set" | "String" |
| "Enable Kubernentes Operator" | "yb.kubernetes.operator.enabled" | "GLOBAL" | "Enable Kubernentes Operator, requires restart to take effect" | "Boolean" |
| "Change the namespace kubernetes operator listens on" | "yb.kubernetes.operator.namespace" | "GLOBAL" | "Change the namespace kubernetes operator listens on. By default, all namespaces are watched. Requires restart to take effect" | "String" |
| "Whether to wait for clock skew decrease" | "yb.wait_for_clock_sync.inline_enabled" | "GLOBAL" | "Whether to wait for the clock skew to go below the threshold before starting the master and tserver processes" | "Boolean" |
| "Delete Expired Backup MAX GC Size" | "yb.backup.delete_expired_backup_max_gc_size" | "GLOBAL" | "Number of expired backups to be deleted in a single GC iteration." | "Integer" |
| "Prometheus external URL" | "yb.metrics.external.url" | "GLOBAL" | "URL used to generate Prometheus metrics on YBA UI and to set up HA metrics federation." | "String" |
| "Prometheus link use browser FQDN" | "yb.metrics.link.use_browser_fqdn" | "GLOBAL" | "If Prometheus link in browser should point to current FQDN in browser or use value from backend." | "Boolean" |
| "OIDC feature enhancements" | "yb.security.oidc_feature_enhancements" | "GLOBAL" | "Enables the OIDC enhancements such as auth_token retrieval, user registration in YBA on login, etc." | "Boolean" |
| "Devops command timeout" | "yb.devops.command_timeout" | "GLOBAL" | "Devops command timeout" | "Duration" |
| "Node destroy command timeout" | "yb.node_ops.destroy_server_timeout" | "GLOBAL" | "Timeout for node destroy command before failing." | "Duration" |
| "YBC Compatible DB Version" | "ybc.compatible_db_version" | "GLOBAL" | "Minimum YBDB version which supports YBC" | "String" |
| "Force YBC Shutdown during upgrade" | "ybc.upgrade.force_shutdown" | "GLOBAL" | "For YBC Shutdown during upgrade" | "Boolean" |
| "Enable strict mode to ignore deprecated YBA APIs" | "yb.api.mode.strict" | "GLOBAL" | "Will ignore deprecated APIs" | "Boolean" |
| "Enable safe mode to ignore preview YBA APIs" | "yb.api.mode.safe" | "GLOBAL" | "Will ignore preview APIs" | "Boolean" |
| "Enable publishing thread dumps to GCS" | "yb.diag.thread_dumps.gcs.enabled" | "GLOBAL" | "Enable publishing thread dumps to GCS" | "Boolean" |
| "Enable publishing support bundles to GCS" | "yb.diag.support_bundles.gcs.enabled" | "GLOBAL" | "Enable publishing support bundles to GCS" | "Boolean" |
| "Operator owned resources api block" | "yb.kubernetes.operator.block_api_operator_owned_resources" | "GLOBAL" | "A resource controlled by the kubernetes operator cannot be updated using the REST API" | "Boolean" |
| "Granular level metrics" | "yb.ui.feature_flags.granular_metrics" | "GLOBAL" | "View granular level metrics when user selects specific time period in a chart" | "Boolean" |
| "Enable multiline option for GFlag conf." | "yb.ui.feature_flags.gflag_multiline_conf" | "GLOBAL" | "Allows user to enter postgres hba rules and ident map rules in multiple rows" | "Boolean" |
| "Disable hostname cert validation for HA communication" | "yb.ha.ws.ssl.loose.disableHostnameVerification" | "GLOBAL" | "When set, the hostname in https certs will not be validated for HA communication. Communication will still be encrypted." | "Boolean" |
| "HA test connection request timeout" | "yb.ha.test_request_timeout" | "GLOBAL" | "The request to test HA connection to standby will timeout after the specified amount of time." | "Duration" |
| "HA test connection connection timeout" | "yb.ha.test_connection_timeout" | "GLOBAL" | "The client will wait for the specified amount of time to make a connection to the remote address." | "Duration" |
| "Crash YBA if Kubernentes Operator thread fails" | "yb.kubernetes.operator.crash_yba_on_operator_failure" | "GLOBAL" | "If Kubernetes Operator thread fails, crash YBA" | "Boolean" |
| "XCluster isBootstrapRequired rpc max parallel threads" | "yb.xcluster.is_bootstrap_required_rpc_pool.max_threads" | "GLOBAL" | "Sets the maximum allowed number of threads to be run concurrently for xcluster isBootstrapRequired rpc" | "Integer" |
| "Auto create user on SSO login" | "yb.security.oidc_enable_auto_create_users" | "GLOBAL" | "Enable user creation on SSO login" | "Boolean" |
| "Kubernetes provider validation" | "yb.provider.kubernetes_provider_validation" | "GLOBAL" | "Enable the Kubernetes provider quick validation" | "Boolean" |
| "AWS provider validation" | "yb.provider.aws_provider_validation" | "GLOBAL" | "Enable AWS Provider quick validation" | "Boolean" |
| "OnPrem provider validation" | "yb.provider.onprem_provider_validation" | "GLOBAL" | "Enable OnPrem Provider quick validation" | "Boolean" |
| "YBC poll upgrade result tries" | "ybc.upgrade.poll_result_tries" | "GLOBAL" | "YBC poll upgrade result tries count." | "Integer" |
| "YBC poll upgrade result Sleep time" | "ybc.upgrade.poll_result_sleep_ms" | "GLOBAL" | "YBC poll upgrade result sleep time." | "Long" |
| "HA Shutdown Level" | "yb.ha.shutdown_level" | "GLOBAL" | "When to shutdown - 0 for never, 1 for promotion, 2 for promotion and demotion" | "Integer" |
| "OIDC Refresh Access Token Interval" | "yb.security.oidcRefreshTokenInterval" | "GLOBAL" | "If configured, YBA will refresh the access token at the specified duration, defaulted to 5 minutes." | "Duration" |
| "Allow Editing of in-use Linux Versions" | "yb.edit_provider.new.allow_used_bundle_edit" | "GLOBAL" | "Caution: If enabled, YBA will blindly allow editing the name/AMI associated with the bundle, without propagating it to the in-use Universes" | "Boolean" |
| "Enable DB Audit Logging" | "yb.universe.audit_logging_enabled" | "GLOBAL" | "If this flag is enabled, user will be able to create telemetry providers and enable/disable DB audit logging on universes." | "Boolean" |
| "Enable Metrics Export" | "yb.universe.metrics_export_enabled" | "GLOBAL" | "If this flag is enabled, user will be able to create telemetry providers and enable/disable metrics export on universes." | "Boolean" |
| "Allow users to enable or disable connection pooling" | "yb.universe.allow_connection_pooling" | "GLOBAL" | "If this flag is enabled, user will be able to enable/disable connection pooling on universes." | "Boolean" |
| "XCluster Sync Scheduler Interval" | "yb.xcluster.xcluster_sync_scheduler_interval" | "GLOBAL" | "Interval at which the XCluster Sync Scheduler runs" | "Duration" |
| "XCluster Metrics Scheduler Interval" | "yb.xcluster.xcluster_metrics_scheduler_interval" | "GLOBAL" | "Interval at which the XCluster Metrics Scheduler runs" | "Duration" |
| "Max retries on UNAVAILABLE status" | "ybc.client_settings.max_unavailable_retries" | "GLOBAL" | "Max client side retries when server returns UNAVAILABLE status" | "Integer" |
| "Wait( in milliseconds ) between each retries on UNAVAILABLE status" | "ybc.client_settings.wait_each_unavailable_retry_ms" | "GLOBAL" | "Wait( in milliseconds ) between client side retries when server returns UNAVAILABLE status" | "Integer" |
| "Wait( in milliseconds ) for YB-Controller RPC response" | "ybc.client_settings.deadline_ms" | "GLOBAL" | "Wait( in milliseconds ) for YB-Controller RPC response before throwing client-side DEADLINE_EXCEEDED" | "Integer" |
| "Enable RBAC for Groups" | "yb.security.group_mapping_rbac_support" | "GLOBAL" | "Map LDAP/OIDC groups to custom roles defined by RBAC." | "Boolean" |
| "Enable Per Process Metrics" | "yb.ui.feature_flags.enable_per_process_metrics" | "GLOBAL" | "Enable Per Process Metrics" | "Boolean" |
| "Node Agent Enabler Run Installer" | "yb.node_agent.enabler.run_installer" | "GLOBAL" | "Enable or disable the background installer in node agent enabler" | "Boolean" |
| "Support bundle prometheus dump range" | "yb.support_bundle.default_prom_dump_range" | "GLOBAL" | "The start-end duration to collect the prometheus dump inside the support bundle (in minutes)" | "Integer" |
| "Number of cloud YBA backups to retain" | "yb.auto_yba_backups.num_cloud_retention" | "GLOBAL" | "When continuous backups feature is enabled only the most recent n backups will be retained in the storage bucket" | "Integer" |
| "Standby Prometheus scrape interval" | "yb.metrics.scrape_interval_standby" | "GLOBAL" | "Need to increase it in case federation metrics request takes more time  than main Prometheus scrape period to complete" | "String" |
| "Enable viewing metrics in timezone selected at the metrics page" | "yb.ui.metrics.enable_timezone" | "GLOBAL" | "Enable viewing metrics in timezone selected at the metrics page and will be preserved at session level" | "Boolean" |
| "Enable Path Access Style for Amazon S3" | "yb.ui.feature_flags.enable_path_style_access" | "GLOBAL" | "Enable Path Access Style for Amazon S3, mainly used when configuring S3 compatible storage." | "Boolean" |
| "Restore YBA postgres metadata during Yugaware container restart" | "yb.ha.k8s_restore_skip_dump_file_delete" | "GLOBAL" | "Restore YBA postgres metadata during Yugaware container restart" | "Boolean" |
| "Node Agent Server Cert Expiry Notice" | "yb.node_agent.server_cert_expiry_notice" | "GLOBAL" | "Duration to start notifying about expiry before node agent server cert actually expires" | "Duration" |
| "Disable Node Agent Configure Server" | "yb.node_agent.disable_configure_server" | "GLOBAL" | "Disable server configuration RPCs in node agent. Defaults to ansible if it is enabled." | "Boolean" |
| "Enable Node Agent Message Compression" | "yb.node_agent.enable_message_compression" | "GLOBAL" | "Enable compression for message sent over node agent channel." | "Boolean" |
| "GCP Blob Delete Retry Count" | "yb.gcp.blob_delete_retry_count" | "GLOBAL" | "Number of times to retry deleting blobs in GCP. This is used to handle the case where the blob deletion fails due to some transient error." | "Integer" |
| "Node Agent Client Connection Cache Size" | "yb.node_agent.connection_cache_size" | "GLOBAL" | "Cache size for node agent client connections" | "Integer" |
| "Ignore Node Agent Client Connection Cache Size" | "yb.node_agent.ignore_connection_cache_size" | "GLOBAL" | "Ignore the cache size (limit) for node agent client connections" | "Boolean" |
| "Node Agent Client Connection Time-out" | "yb.node_agent.connect_timeout" | "GLOBAL" | "Client connection time-out for node agent." | "Duration" |
| "Node Agent Client Idle Connection Time-out" | "yb.node_agent.idle_connection_timeout" | "GLOBAL" | "Client idle connection timeout for node agent." | "Duration" |
| "Node Agent Client Keep Alive Time" | "yb.node_agent.connection_keep_alive_time" | "GLOBAL" | "Client connection keep-alive time for node agent." | "Duration" |
| "Node Agent Client Keep Alive Time-out" | "yb.node_agent.connection_keep_alive_timeout" | "GLOBAL" | "Client connection keep-alive timeout for node agent." | "Duration" |
| "Node Agent Describe Poll Deadline" | "yb.node_agent.describe_poll_deadline" | "GLOBAL" | "Node agent describe polling deadline." | "Duration" |
| "Allow Cloud Volume Encryption feature" | "yb.universe.allow_cloud_volume_encryption" | "GLOBAL" | "Allows enabling the volume encryption feature for new universes. Currently only supported for AWS universes." | "Boolean" |
| "Enable YBC Background Upgrade" | "ybc.upgrade.enable_background_upgrade" | "GLOBAL" | "Enable background upgrade for YBC." | "Boolean" |
| "Enable Systemd Debug Logging" | "yb.ansible.systemd_debug" | "GLOBAL" | "Enable systemd debug logging for systemctl service management commands." | "Boolean" |
| "Keep Remote Files from an ansible run" | "yb.ansible.keep_remote_files" | "GLOBAL" | "Keep remote files after ansible run for debugging." | "Boolean" |
| "Skip Runtime GFlag validation before cluster operations." | "yb.skip_runtime_gflag_validation" | "GLOBAL" | "Skip Runtime GFlag validation before cluster operations." | "Boolean" |
| "Timeout for backup success marker download" | "ybc.success_marker_download_timeout_secs" | "GLOBAL" | "Timeout for backup success marker download from backup location" | "Integer" |
| "Enable Performing Automatic Rollback of Edit Operation" | "yb.task.enable_edit_auto_rollback" | "GLOBAL" | "Enable performing automatic rollback of edit operation (if possible)" | "Boolean" |
| "Enable S3 Backup Proxy" | "yb.ui.feature_flags.enable_s3_backup_proxy" | "GLOBAL" | "Enable proxy configuration for S3 backup storage" | "Boolean" |
| "Allow YBA Restore With Universes" | "yb.yba_backup.allow_restore_with_universes" | "GLOBAL" | "Allow YBA restore from one time restore or continuous backup when existing universes are present" | "Boolean" |
| "Allow YBA Restore With Old Backup" | "yb.yba_backup.allow_restore_with_old_backup" | "GLOBAL" | "Allow YBA restore from one time restore or continuous backup when backup file is more than 1 day old" | "Boolean" |
| "Allow Local Login with SSO" | "yb.security.allow_local_login_with_sso" | "GLOBAL" | "Allow local user login with SSO enabled. when disabled, only superAdmin can login using local credentials." | "Boolean" |
| "Node Agent Server Log Level Per Request" | "yb.node_agent.server.request_log_level" | "GLOBAL" | "Log level for Node Agent server per request (0 for debug, -1 for default)" | "Integer" |
| "Disable Platform HA Restore Transaction" | "yb.ha.disable_platform_ha_restore_transaction" | "GLOBAL" | "Disable running platform HA restore operations in a transaction" | "Boolean" |
| "Clock Skew" | "yb.alert.max_clock_skew_ms" | "UNIVERSE" | "Default threshold for Clock Skew alert" | "Duration" |
| "Health Log Output" | "yb.health.logOutput" | "UNIVERSE" | "It determines whether to log the output of the node health check script to the console" | "Boolean" |
| "Node Checkout Time" | "yb.health.nodeCheckTimeoutSec" | "UNIVERSE" | "The timeout (in seconds) for node check operation as part of universe health check" | "Integer" |
| "Node Checkout Time for DDL check" | "yb.health.nodeCheckTimeoutDdlSec" | "UNIVERSE" | "The timeout (in seconds) for node check operation as part of universe health check in case DDL atomicity check is performed" | "Integer" |
| "DDL Atomicity Check Enabled" | "yb.health.ddl_atomicity_check_enabled" | "UNIVERSE" | "If we want to perform DDL atomicity check for the universe periodically" | "Boolean" |
| "DDL Atomicity Check Interval" | "yb.health.ddl_atomicity_interval_sec" | "UNIVERSE" | "The interval (in seconds) between DDL atomicity checks" | "Integer" |
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
| "Metrics Collection Level" | "yb.metrics.collection_level" | "UNIVERSE" | "DB node metrics collection level.ALL - collect all metrics, NORMAL - default value, which only limits some per-table metrics, TABLE_OFF - Disable table level metrics collection, MINIMAL - limits both node level and further limits table levelmetrics we collect and OFF to completely disable metric collection." | "String" |
| "Universe Version Check Mode" | "yb.universe_version_check_mode" | "UNIVERSE" | "Possible values: NEVER, HA_ONLY, ALWAYS" | "VersionCheckMode" |
| "Override Force Universe Lock" | "yb.task.override_force_universe_lock" | "UNIVERSE" | "Whether overriding universe lock is allowed when force option is selected.If it is disabled, force option will wait for the lock to be released." | "Boolean" |
| "Enable SSH Key Expiration" | "yb.security.ssh_keys.enable_ssh_key_expiration" | "UNIVERSE" | "TODO" | "Boolean" |
| "SSh Key Expiration Threshold" | "yb.security.ssh_keys.ssh_key_expiration_threshold_days" | "UNIVERSE" | "TODO" | "Integer" |
| "Enable SSE" | "yb.backup.enable_sse" | "UNIVERSE" | "Enable SSE during backup/restore" | "Boolean" |
| "Allow Table by Table backups for YCQL" | "yb.backup.allow_table_by_table_backup_ycql" | "UNIVERSE" | "Backup tables individually during YCQL backup" | "Boolean" |
| "NFS Directry Path" | "yb.ybc_flags.nfs_dirs" | "UNIVERSE" | "Authorised NFS directories for backups" | "String" |
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
| "Slow queries retention period" | "yb.query_stats.slow_queries.retention_period_days" | "UNIVERSE" | "Data retention period (in days) if slow query aggregation is enabled." | "Integer" |
| "Disable Slow queries aggregation" | "yb.query_stats.slow_queries.disable_aggregation" | "UNIVERSE" | "If enabled, slow queries data will be stored for universe, once per hour." | "Boolean" |
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
| "Connection Pooling logs regex pattern" | "yb.support_bundle.connection_pooling_logs_regex_pattern" | "UNIVERSE" | "Connection Pooling logs regex pattern in support bundle" | "String" |
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
| "YSQL Queries Timeout for Consistency Check Operations" | "yb.universe.consistency_check.ysql_timeout_secs" | "UNIVERSE" | "Timeout in secs for YSQL queries" | "Long" |
| "Number of cores to keep" | "yb.num_cores_to_keep" | "UNIVERSE" | "Controls the configuration to set the number of cores to keep in the Ansible layer" | "Integer" |
| "Whether to check YBA xCluster object is in sync with DB replication group" | "yb.xcluster.ensure_sync_get_replication_status" | "UNIVERSE" | "It ensures that the YBA XCluster object for tables that are in replication is in sync with replication group in DB. If they are not in sync and this is true, getting the xCluster object will throw an exception and the user has to resync the xCluster config." | "Boolean" |
| "Network Load balancer health check ports" | "yb.universe.network_load_balancer.custom_health_check_ports" | "UNIVERSE" | "Ports to use for health checks performed by the network load balancer. Invalid and duplicate ports will be ignored. For GCP, only the first health check port would be used." | "Integer List" |
| "Network Load balancer health check protocol" | "yb.universe.network_load_balancer.custom_health_check_protocol" | "UNIVERSE" | "Protocol to use for health checks performed by the network load balancer" | "Protocol" |
| "Network Load balancer health check paths" | "yb.universe.network_load_balancer.custom_health_check_paths" | "UNIVERSE" | "Paths probed by HTTP/HTTPS health checks performed by the network load balancer. Paths are mapped one-to-one with the custom health check ports runtime configuration." | "String List" |
| "Validate filepath for local release" | "yb.universe.validate_local_release" | "UNIVERSE" | "For certain tasks validates the existence of local filepath for the universe software version." | "Boolean" |
| "The delay before the next poll of the PITR config creation status" | "yb.pitr.create_poll_delay" | "UNIVERSE" | "It is the delay after which the create PITR config subtask rechecks the status of the PITR config creation in each iteration" | "Duration" |
| "The delay before the next poll of the PITR config restore status" | "yb.pitr.restore_poll_delay" | "UNIVERSE" | "It is the delay after which the restore PITR config subtask rechecks the status of the restore operation" | "Duration" |
| "The timeout for restoring a universe using a PITR config" | "yb.pitr.restore_timeout" | "UNIVERSE" | "It is the maximum time that the restore PITR config subtask waits for the restore operation using PITR to be completed; otherwise, it will fail the operation" | "Duration" |
| "The timeout for creating a PITR config" | "yb.pitr.create_timeout" | "UNIVERSE" | "It is the maximum time that the create PITR config subtask waits for the PITR config to be created; otherwise, it will fail the operation" | "Duration" |
| "Enable network connectivity check for xCluster" | "yb.xcluster.network_connectivity_check.enabled" | "UNIVERSE" | "If this flag is true on the source universe, a ping and port accessibility check from each node of the target universe to all the source universe nodes will be performed" | "Boolean" |
| "The timeout used for network connectivity check for xCluster setup" | "yb.xcluster.network_connectivity_check.ping_command_timeout" | "UNIVERSE" | "The network connectivity check for xCluster ping all the source nodes from the target nodes; this is the timeout used to indicate how long the ping command should wait for the response" | "Duration" |
| "Default PITR retention period for txn xCluster" | "yb.xcluster.transactional.pitr.default_retention_period" | "UNIVERSE" | "The default retention period used to create PITR configs for transactional xCluster replication; it will be used when there is no existing PITR configs and it is not specified in the task parameters" | "Duration" |
| "Default PITR snapshot interval for txn xCluster" | "yb.xcluster.transactional.pitr.default_snapshot_interval" | "UNIVERSE" | "The default snapshot interval used to create PITR configs for transactional xCluster replication; it will be used when there is no existing PITR configs and it is not specified in the task parameters" | "Duration" |
| "Allow multiple txn replication configs" | "yb.xcluster.transactional.allow_multiple_configs" | "UNIVERSE" | "Allow multiple txn replication configs" | "Boolean" |
| "Skip backup metadata validation" | "yb.backup.skip_metadata_validation" | "UNIVERSE" | "Skip backup metadata based validation during restore" | "Boolean" |
| "Parallelism for Node Agent Reinstallation" | "yb.node_agent.reinstall_parallelism" | "UNIVERSE" | "Number of parallel node agent reinstallations at a time" | "Integer" |
| "Sync user-groups between the Universe DB nodes and LDAP Server" | "yb.security.ldap.ldap_universe_sync" | "UNIVERSE" | "If configured, this feature allows users to synchronise user groups configured on the upstream LDAP Server with user roles in YBDB nodes associated with the universe." | "Boolean" |
| "Cluster membership check timeout" | "yb.checks.cluster_membership.timeout" | "UNIVERSE" | "Controls the max time to check that there are no tablets assigned to the node" | "Duration" |
| "Verify current cluster state (from db perspective) before running task" | "yb.task.verify_cluster_state" | "UNIVERSE" | "Verify current cluster state (from db perspective) before running task" | "Boolean" |
| "Wait time for xcluster/DR replication setup and edit RPCs" | "yb.xcluster.operation_timeout" | "UNIVERSE" | "Wait time for xcluster/DR replication setup and edit RPCs." | "Duration" |
| "Maximum timeout for xCluster bootstrap producer RPC call" | "yb.xcluster.bootstrap_producer_timeout" | "UNIVERSE" | "If the RPC call to create the bootstrap streams on the source universe does not return before this timeout, the task will retry with exponential backoff until it fails." | "Duration" |
| "Maximum timeout for delete replication RPC on source during failover" | "yb.xcluster.db_scoped.failover.delete_replication_on_source_timeout" | "UNIVERSE" | "If the source universe is down, this RPC call will time out during failover operation, increasing the failover task execution time; The lower the value, the less time the failover task will take to complete. If it is set to zero, this subtask during failover will be skipped providing a faster failover execution time." | "Duration" |
| "Enable xCluster DR Semi-automatic Mode" | "yb.xcluster.db_scoped.creationEnabled" | "UNIVERSE" | "When enabled, new xCluster DR configurations use Semi-automatic mode." | "Boolean" |
| "Enable xCluster DR Automatic Mode" | "yb.xcluster.db_scoped.automatic_ddl.creationEnabled" | "UNIVERSE" | "When this and yb.xcluster.db_scoped.creationEnabled are both enabled, new xCluster DR configurations use Automatic mode." | "Boolean" |
| "Leaderless tablets check enabled" | "yb.checks.leaderless_tablets.enabled" | "UNIVERSE" | " Whether to run CheckLeaderlessTablets subtask before running universe tasks" | "Boolean" |
| "Leaderless tablets check timeout" | "yb.checks.leaderless_tablets.timeout" | "UNIVERSE" | "Controls the max time out when performing the CheckLeaderlessTablets subtask" | "Duration" |
| "Enable Clock Sync check" | "yb.wait_for_clock_sync.enabled" | "UNIVERSE" | "Enable Clock Sync check" | "Boolean" |
| "Enable YBC" | "ybc.universe.enabled" | "UNIVERSE" | "Enable YBC for universes during software upgrade" | "Boolean" |
| "Target Node Disk Usage Percentage" | "yb.checks.node_disk_size.target_usage_percentage" | "UNIVERSE" | "Percentage of current disk usage that may consume on the target nodes" | "Integer" |
| "Enable Automated Master Failover" | "yb.auto_master_failover.enabled" | "UNIVERSE" | "Enable Automated Master Failover for universes in background process" | "Boolean" |
| "Master Follower Lag Soft Threshold" | "yb.auto_master_failover.master_follower_lag_soft_threshold" | "UNIVERSE" | "Master follower lag soft threshold for potential master failure" | "Duration" |
| "Master Follower Lag Hard Threshold" | "yb.auto_master_failover.master_follower_lag_hard_threshold" | "UNIVERSE" | "Master follower lag hard threshold for definite master failure" | "Duration" |
| "Stop multiple nodes in az simultaneously during upgrade" | "yb.task.upgrade.batch_roll_enabled" | "UNIVERSE" | "Stop multiple nodes in az simultaneously during upgrade" | "Boolean" |
| "Stop multiple nodes in az simultaneously during upgrade (in k8s)" | "yb.task.upgrade.batch_roll_enabled_k8s" | "UNIVERSE" | "Stop multiple nodes in az simultaneously during upgrade (in k8s)" | "Boolean" |
| "Max percent of nodes to roll simultaneously during upgrade" | "yb.task.upgrade.batch_roll_auto_percent" | "UNIVERSE" | "Max percent of nodes to roll simultaneously during upgrade" | "Integer" |
| "Max number of nodes to roll simultaneously during upgrade" | "yb.task.upgrade.batch_roll_auto_number" | "UNIVERSE" | "Max number of nodes to roll simultaneously during upgrade" | "Integer" |
| "Automated Master Failover Detection Interval" | "yb.auto_master_failover.detect_interval" | "UNIVERSE" | "Automated master failover detection interval for a universe in background process" | "Duration" |
| "Automated Sync Master Addresses Task Delay" | "yb.auto_master_failover.sync_master_addrs_task_delay" | "UNIVERSE" | "Automated sync master addresses task submission delay for a universe in background process" | "Duration" |
| "XCluster Sync on Universe" | "yb.xcluster.xcluster_sync_on_universe" | "UNIVERSE" | "Enable automatic synchronization of XCluster on Universe" | "Boolean" |
| "CPU usage alert aggregation interval" | "yb.alert.cpu_usage_interval_secs" | "UNIVERSE" | "CPU usage alert aggregation interval in seconds." | "Integer" |
| "Enable health checks for time drift between nodes" | "yb.health_checks.check_clock_time_drift" | "UNIVERSE" | "Enable health checks for time drift between nodes." | "Boolean" |
| "Time drift threshold for warning health check" | "yb.health_checks.time_drift_wrn_threshold_ms" | "UNIVERSE" | "Threshold to raise a warning when time drift exceeds this amount" | "Integer" |
| "Time drift threshold for error health check" | "yb.health_checks.time_drift_err_threshold_ms" | "UNIVERSE" | "Threshold to raise a error when time drift exceeds this amount" | "Integer" |
| "Enable consistency check for universe" | "yb.universe.consistency_check.enabled" | "UNIVERSE" | "When enabled, all universe operations will attempt consistency check validation before proceeding. Turn off in disaster scenarios to force perform actions." | "Boolean" |
| "Fail the the health check if no clock sync service is found" | "yb.health_checks.clock_sync_service_required" | "UNIVERSE" | "Require chrony or ntp(d) to be installed for health check to pass" | "Boolean" |
| "Node Agent Enabler Installation Time-out" | "yb.node_agent.enabler.install_timeout" | "UNIVERSE" | "Node agent enabler installation time-out for the universe" | "Duration" |
| "Node Agent Enabler Reinstallation Cooldown Period" | "yb.node_agent.enabler.reinstall_cooldown" | "UNIVERSE" | "Node agent enabler reinstallation cooldown period for the universe" | "Duration" |
| "Option for Off-Cluster PITR based Backup Schedule" | "yb.ui.feature_flags.off_cluster_pitr_enabled" | "UNIVERSE" | "Enable option for creating backup schedules that support off-cluster PITR" | "Boolean" |
| "Use S3 IAM roles attached to DB node for Backup/Restore" | "yb.backup.s3.use_db_nodes_iam_role_for_backup" | "UNIVERSE" | "Use S3 IAM roles attached to DB node for Backup/Restore" | "Boolean" |
| "Queue Wait Time for Tasks" | "yb.task.queue_wait_time" | "UNIVERSE" | "Wait time for a queued task before the running task can be evicted forcefully." | "Duration" |
| "Common Name Required for Certificates" | "yb.tls.cert_manager.common_name_required" | "UNIVERSE" | "If true, YBA will add commonName to the CertificateRequest sent to cert manager." | "Boolean" |
| "Skip OpenTelemetry Operator Check" | "yb.universe.skip_otel_operator_check" | "UNIVERSE" | "If true, YBA will skip checking for Opentelemetry operator installation on the cluster." | "Boolean" |
| "Max memory for OpenTelemetry Collector process." | "yb.universe.otel_collector_max_memory" | "UNIVERSE" | "Hard memory limit for the OpenTelemetry Collector process in the systemd unit file." | "Integer" |
| "Wait Attempts for major catalog upgrade" | "yb.upgrade.wait_attempts_for_major_catalog_upgrade" | "UNIVERSE" | "Wait Attempts for major catalog upgrade" | "Integer" |
| "PG Upgrade Check Timeout" | "yb.upgrade.pg_upgrade_check_timeout_secs" | "UNIVERSE" | "Timeout for pg_upgrade check in seconds" | "Integer" |
| "Allow users to disable DB APIs" | "yb.configure_db_api.allow_disable" | "UNIVERSE" | "Allow users to disable DB APIs" | "Boolean" |
| "Enable Clockbound synchronization check" | "yb.checks.clockbound.enabled" | "UNIVERSE" | "Enable Clock Sync check" | "Boolean" |
| "Clockbound synchronization check timeout" | "yb.checks.clockbound.timeout" | "UNIVERSE" | "Clockbound synchronization check timeout" | "Duration" |
| "Delay between failed create tablespaces operation retry" | "yb.task.create_tablespaces.retry_delay" | "UNIVERSE" | "Delay between failed create tablespaces operation retry" | "Duration" |
| "Timeout for create tablespaces task retries" | "yb.task.create_tablespaces.retry_timeout" | "UNIVERSE" | "Timeout for create tablespaces task retries" | "Duration" |
| "Minimal number of retries for create tablespaces task" | "yb.task.create_tablespaces.min_retries" | "UNIVERSE" | "Minimal number of retries for create tablespaces task" | "Integer" |
| "Whether to alert for unexpected masters/tservers in universe" | "yb.health_checks.unexpected_servers_check_enabled" | "UNIVERSE" | "Whether to alert for unexpected masters/tservers in universe" | "Boolean" |
| "Enable NFS Backup precheck" | "yb.backup.enable_nfs_precheck" | "UNIVERSE" | "Enable/disable check which verifies free space on NFS mount before backup." | "Boolean" |
| "NFS precheck buffer space" | "yb.backup.nfs_precheck_buffer_kb" | "UNIVERSE" | "Amount of space (in KB) we want as buffer for NFS precheck" | "Long" |
| "Wait after each pod restart in rolling operations" | "yb.kubernetes.operator.rolling_ops_wait_after_each_pod_ms" | "UNIVERSE" | "Time to wait after each pod restart before restarting the next pod in rolling operations" | "Integer" |
| "Backup and restore to use pre roles behaviour" | "ybc.revert_to_pre_roles_behaviour" | "UNIVERSE" | "Have YBC use the pre roles backup and restore behaviour" | "Boolean" |
| "Enable backups during DDL" | "yb.backup.enable_backups_during_ddl" | "UNIVERSE" | "Have YBC ysql-dump use read-time as of snapshot time to support backups during DDL" | "Boolean" |
| "Whether to check if correct THP settings are applied" | "yb.health_checks.check_thp" | "UNIVERSE" | "Whether to check if correct Transparent Huge Pages settings are applied" | "Boolean" |
| "Timeout for catalog upgrade admin operations" | "yb.upgrade.catalog_upgrade_admin_ops_timeout_ms" | "UNIVERSE" | "Timeout for catalog upgrade admin operations in milliseconds" | "Long" |
| "Skip auto flags and YSQL migration files validation" | "yb.upgrade.skip_autoflags_and_ysql_migration_files_validation" | "UNIVERSE" | "Skip auto flags and YSQL migration files validation" | "Boolean" |
| "Per disk IO request size" | "ybc.disk_io_request_size_bytes" | "UNIVERSE" | "Per disk IO request size during backup/restore in Yb-Controller" | "Long" |
| "Default disk IO read bytes per second" | "ybc.default_disk_io_read_bytes_per_sec" | "UNIVERSE" | "Default disk IO read bytes per second during backup in Yb-Controller" | "Long" |
| "Default disk IO write bytes per second" | "ybc.default_disk_io_write_bytes_per_sec" | "UNIVERSE" | "Default disk IO write bytes per second during restore in Yb-Controller" | "Long" |
| "Auto Recover from Pending Upgrade" | "yb.helm.auto_recover_from_pending_upgrade" | "UNIVERSE" | "If true, YBA will automatically recover from stuck Helm upgrades before performing Helm upgrade operations" | "Boolean" |
| "Upgrade Master Sleep Time Per AZ" | "yb.upgrade.upgrade_master_stage_pause_duration_ms" | "UNIVERSE" | "Time to sleep after upgrading masters in each AZ" | "Long" |
| "Upgrade TServer Sleep Time Per AZ" | "yb.upgrade.upgrade_tserver_stage_pause_duration_ms" | "UNIVERSE" | "Time to sleep after upgrading tservers in each AZ" | "Long" |
| "Enables new Performance Monitoring UI via Performance Tab if universe is registered with Perf Advisor Service" | "yb.ui.feature_flags.enable_new_perf_advisor_ui" | "UNIVERSE" | "Enables new Performance Monitoring UI via Performance Tab" | "Boolean" |
| "Enable Canary Upgrade" | "yb.upgrade.enable_canary_upgrade" | "UNIVERSE" | "Enable canary upgrade for the universe" | "Boolean" |
