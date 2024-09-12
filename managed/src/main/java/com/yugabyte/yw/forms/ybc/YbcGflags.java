// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.ybc;

import java.util.Map;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.Setter;

@Getter
@Setter
public class YbcGflags {

  // Yugabyte DB Interaction flags
  Boolean k8s;
  String yb_admin;
  String yb_ctl;
  String ysql_dump;
  String ysql_dumpall;
  String rocksdb_rel_path;
  String ysqlsh;
  String ycqlsh;
  String redis_cli;
  String cores_dir;
  Integer exec_timeout_secs;

  // Utility flags
  String tmp_dir;
  Integer hardware_concurrency;
  Integer v;

  // Backup task cleanup related flags
  Integer num_backup_service_threads;
  Integer task_expiry_time_secs;
  Integer gc_interval_secs;
  String certs_dir_name;
  String cert_node_filename;

  // Snapshot based flags
  Integer num_api_threads;
  Integer yb_api_timeout_secs;
  Integer disable_splitting_ms;
  Integer disable_splitting_freq_secs;
  Boolean use_yb_api;
  Integer yb_api_max_idle_secs;
  Integer yb_api_idle_check_secs;

  // Controller object flags( upload/download and related operations )
  Integer max_concurrent_uploads;
  Integer max_concurrent_downloads;
  Integer ops_num_threads;
  Integer rw_num_threads;
  Integer per_upload_num_objects;
  Integer per_download_num_objects;
  Integer object_get_wait_ms;
  Integer max_retries;
  Integer max_timeout_secs;
  Boolean allow_any_nfs_dir;
  String nfs_dirs;
  String checksum_algorithm;
  String curlopt_cainfo;

  @AllArgsConstructor
  public static class YbcGflagsMetadata {
    public String flagName;
    public String meaning;
    public String defaultValue;
    public String type;
  }

  public static Map<String, YbcGflagsMetadata> ybcGflagsMetadata =
      Map.ofEntries(
          Map.entry(
              "cores_dir",
              new YbcGflagsMetadata(
                  "cores_dir", "Path to the cores directory", "/home/yugabyte/cores", "string")),
          Map.entry(
              "exec_timeout_secs",
              new YbcGflagsMetadata(
                  "exec_timeout_secs",
                  "Default timeout in seconds for any external command execution",
                  "86400",
                  "int32")),
          Map.entry(
              "k8s",
              new YbcGflagsMetadata("k8s", "Is this running in Kubernetes", "false", "bool")),
          Map.entry(
              "redis_cli",
              new YbcGflagsMetadata(
                  "redis_cli",
                  "Path to redis-cli",
                  "/home/yugabyte/tserver/bin/redis-cli",
                  "string")),
          Map.entry(
              "rocksdb_rel_path",
              new YbcGflagsMetadata(
                  "rocksdb_rel_path",
                  "Path to the rocksdb data directory, relative to fs_data_dirs",
                  "/yb-data/tserver/data/rocksdb/",
                  "string")),
          Map.entry(
              "yb_admin",
              new YbcGflagsMetadata(
                  "yb_admin", "Path to yb-admin", "/home/yugabyte/tserver/bin/yb-admin", "string")),
          Map.entry(
              "yb_ctl",
              new YbcGflagsMetadata(
                  "yb_ctl", "Path to yb-ctl", "/home/yugabyte/tserver/bin/yb-ctl", "string")),
          Map.entry(
              "yb_master_address",
              new YbcGflagsMetadata("yb_master_address", "address of yb-master", "", "string")),
          Map.entry(
              "yb_master_webserver_port",
              new YbcGflagsMetadata(
                  "yb_master_webserver_port", "yb-master webserver port", "7000", "int32")),
          Map.entry(
              "yb_tserver_address",
              new YbcGflagsMetadata(
                  "yb_tserver_address", "address of yb-tserver", "127.0.0.1", "string")),
          Map.entry(
              "yb_tserver_webserver_port",
              new YbcGflagsMetadata(
                  "yb_tserver_webserver_port", "yb-tserver webserver port", "9000", "int32")),
          Map.entry(
              "ycqlsh",
              new YbcGflagsMetadata(
                  "ycqlsh", "Path to ycqlsh", "/home/yugabyte/tserver/bin/ycqlsh", "string")),
          Map.entry(
              "ysql_dump",
              new YbcGflagsMetadata(
                  "ysql_dump",
                  "Path to ysql_dump",
                  "/home/yugabyte/tserver/postgres/bin/ysql_dump",
                  "string")),
          Map.entry(
              "ysql_dumpall",
              new YbcGflagsMetadata(
                  "ysql_dumpall",
                  "Path to ysql_dumpall",
                  "/home/yugabyte/tserver/postgres/bin/ysql_dumpall",
                  "string")),
          Map.entry(
              "allow_any_nfs_dir",
              new YbcGflagsMetadata(
                  "allow_any_nfs_dir", "Allow use of any directory as nfs dir.", "true", "bool")),
          Map.entry(
              "ca_bundle_expiry_secs",
              new YbcGflagsMetadata(
                  "ca_bundle_expiry_secs",
                  "Idle time before a ca-bundle is deleted",
                  "90000",
                  "int32")),
          Map.entry(
              "checksum_algorithm",
              new YbcGflagsMetadata(
                  "checksum_algorithm",
                  "Checksum algorithm to use. Available options are SHA512, SHA256, XXH3_64,"
                      + " XXH3_128",
                  "XXH3_64",
                  "string")),
          Map.entry(
              "curlopt_cainfo",
              new YbcGflagsMetadata(
                  "curlopt_cainfo",
                  "System certificate bundle, defaults to one of /etc/pki/tls/certs/ca-bundle.crt,"
                      + " /etc/ssl/certs/ca-certificates.crt or /etc/ssl/cert.pem depending on OS",
                  "",
                  "string")),
          Map.entry(
              "max_concurrent_downloads",
              new YbcGflagsMetadata(
                  "max_concurrent_downloads",
                  "Max number of concurrent download tasks",
                  "2",
                  "int32")),
          Map.entry(
              "max_concurrent_uploads",
              new YbcGflagsMetadata(
                  "max_concurrent_uploads", "Max number of concurrent upload tasks", "2", "int32")),
          Map.entry(
              "max_retries",
              new YbcGflagsMetadata("max_retries", "Max number of retries.", "10", "int32")),
          Map.entry(
              "max_timeout_secs",
              new YbcGflagsMetadata(
                  "max_timeout_secs", "Max timeout between retry.", "60", "int32")),
          Map.entry(
              "nfs_dirs",
              new YbcGflagsMetadata(
                  "nfs_dirs",
                  "Comma separated list of nfs dirs that are authorized for use.",
                  "/nfs,/tmp/nfs",
                  "string")),
          Map.entry(
              "object_get_wait_ms",
              new YbcGflagsMetadata(
                  "object_get_wait_ms",
                  "Time in milliseconds to wait for an object to be available in the pool.",
                  "200",
                  "int32")),
          Map.entry(
              "ops_num_threads",
              new YbcGflagsMetadata(
                  "ops_num_threads",
                  "Number of threads to use for upload/download operations",
                  "0",
                  "int32")),
          Map.entry(
              "per_download_num_objects",
              new YbcGflagsMetadata(
                  "per_download_num_objects",
                  "Number of objects in pool for each download operation",
                  "0",
                  "int32")),
          Map.entry(
              "per_upload_num_objects",
              new YbcGflagsMetadata(
                  "per_upload_num_objects",
                  "Number of objects in pool for each upload operation",
                  "0",
                  "int32")),
          Map.entry(
              "rw_num_threads",
              new YbcGflagsMetadata(
                  "rw_num_threads",
                  "Number of threads to use for read/write operations",
                  "0",
                  "int32")),
          Map.entry(
              "only_bind",
              new YbcGflagsMetadata("only_bind", "Do only bind and exit", "false", "bool")),
          Map.entry(
              "disable_splitting_freq_secs",
              new YbcGflagsMetadata(
                  "disable_splitting_freq_secs",
                  "Frequency at which to keep disabling tablet splitting while backup/restore is in"
                      + " progress",
                  "60",
                  "int32")),
          Map.entry(
              "disable_splitting_ms",
              new YbcGflagsMetadata(
                  "disable_splitting_ms",
                  "How long to disable tablet splitting for",
                  "120000",
                  "int32")),
          Map.entry(
              "num_api_threads",
              new YbcGflagsMetadata(
                  "num_api_threads", "Number of threads to use for yb api", "0", "int32")),
          Map.entry(
              "use_yb_api",
              new YbcGflagsMetadata(
                  "use_yb_api", "Use the yb api (if true) else use yb_admin", "true", "bool")),
          Map.entry(
              "yb_api_idle_check_secs",
              new YbcGflagsMetadata(
                  "yb_api_idle_check_secs",
                  "Time between checks for idle yb_api connections",
                  "360",
                  "int32")),
          Map.entry(
              "yb_api_max_idle_secs",
              new YbcGflagsMetadata(
                  "yb_api_max_idle_secs",
                  "Max idle time before the yb_api connection is removed",
                  "300",
                  "int32")),
          Map.entry(
              "yb_api_timeout_secs",
              new YbcGflagsMetadata("yb_api_timeout_secs", "Timeout for Yb APIs", "600", "int32")),
          Map.entry(
              "yb_task_timeout_secs",
              new YbcGflagsMetadata(
                  "yb_task_timeout_secs",
                  "Timeout for async tasks (such as snapshot create) launched on YB",
                  "7200",
                  "int32")),
          Map.entry(
              "log_filename",
              new YbcGflagsMetadata(
                  "log_filename",
                  "Prefix of log filename - full path is"
                      + " <log_dir>/<log_filename>.[INFO|WARN|ERROR|FATAL]",
                  "",
                  "string")),
          Map.entry(
              "hardware_concurrency",
              new YbcGflagsMetadata(
                  "hardware_concurrency", "Number of cores to use", "2", "int32")),
          Map.entry(
              "tmp_dir",
              new YbcGflagsMetadata("tmp_dir", "dir to use for tmp files", "/tmp", "string")),
          Map.entry(
              "ysqlsh",
              new YbcGflagsMetadata(
                  "ysqlsh",
                  "Path to ysqlsh",
                  "/home/yugabyte/tserver/postgres/bin/ysqlsh",
                  "string")));
}
