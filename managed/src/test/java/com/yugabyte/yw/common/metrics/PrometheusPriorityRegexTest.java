// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common.metrics;

import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.TestUtils;
import java.util.HashSet;
import java.util.Set;
import java.util.regex.Pattern;
import org.junit.Test;

// This validates the priority-regex in prometheus config present in
// replicated.yaml.
public class PrometheusPriorityRegexTest {

  public static String[] expectedMetricNames =
      new String[] {
        "rocksdb_number_db_seek",
        "rocksdb_number_db_next",
        "rocksdb_current_version_sst_files_size",
        "rocksdb_current_version_num_sst_files",
        "rocksdb_bloom_filter_checked",
        "rocksdb_bloom_filter_useful",
        "rocksdb_db_get_micros_sum",
        "rocksdb_db_get_micros_count",
        "rocksdb_db_write_micros_sum",
        "rocksdb_db_write_micros_count",
        "rocksdb_db_seek_micros_sum",
        "rocksdb_db_seek_micros_count",
        "rocksdb_db_mutex_wait_micros",
        "rocksdb_block_cache_hit",
        "rocksdb_block_cache_miss",
        "rocksdb_block_cache_add",
        "rocksdb_block_cache_single_touch_add",
        "rocksdb_block_cache_multi_touch_add",
        "rocksdb_stall_micros",
        "rocksdb_flush_write_bytes",
        "rocksdb_compact_read_bytes",
        "rocksdb_compact_write_bytes",
        "rocksdb_numfiles_in_singlecompaction_sum",
        "rocksdb_numfiles_in_singlecompaction_count",
        "rocksdb_compaction_times_micros_sum",
        "rocksdb_compaction_times_micros_count",
        "async_replication_sent_lag_micros",
        "async_replication_committed_lag_micros",
        "majority_sst_files_rejections",
        "expired_transactions",
        "log_sync_latency_sum",
        "log_sync_latency_count",
        "log_group_commit_latency_sum",
        "log_group_commit_latency_count",
        "log_append_latency_sum",
        "log_append_latency_count",
        "log_bytes_logged",
        "log_reader_bytes_read",
        "log_append_latency_count",
        "log_group_commit_latency_count",
        "log_sync_latency_count",
        "log_cache_size",
        "log_cache_num_ops",
        "follower_lag_ms",
        "leader_memory_pressure_rejections",
        "follower_memory_pressure_rejections",
        "operation_memory_pressure_rejections"
      };

  @Test
  public void testPrometheusPriorityRegex() {
    JsonNode params = TestUtils.readResourceAsJson("metric/normal_level_params.json");
    StringBuilder regex = new StringBuilder();
    params.get("priority_regex").forEach(line -> regex.append(line.textValue()));
    Pattern pattern = Pattern.compile(regex.toString());
    Set<String> notMatched = new HashSet<>();
    for (String metric : expectedMetricNames) {
      if (!pattern.matcher(metric).find()) {
        notMatched.add(metric);
      }
    }
    assertTrue("All regex must match. Unmatched regex: " + notMatched, notMatched.isEmpty());
  }
}
