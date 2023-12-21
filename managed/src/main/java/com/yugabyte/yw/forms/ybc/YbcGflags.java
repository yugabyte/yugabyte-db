// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms.ybc;

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
}
