// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common.metrics;

import static org.junit.Assert.assertTrue;

import java.io.FileInputStream;
import java.io.InputStream;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.regex.Pattern;
import org.junit.Test;
import org.yaml.snakeyaml.Yaml;

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
        "glog_info_messages",
        "glog_warning_messages",
        "glog_error_messages",
        "follower_lag_ms",
        "leader_memory_pressure_rejections",
        "follower_memory_pressure_rejections",
        "operation_memory_pressure_rejections"
      };

  @SuppressWarnings("unchecked")
  public String getPrometheusYamlString() {
    Yaml yaml = new Yaml();
    String replicatedFilepath = System.getProperty("user.dir") + "/devops/replicated.yml";
    try (InputStream in = new FileInputStream(replicatedFilepath)) {
      Map<String, Object> map = yaml.load(in);
      List<Object> components = (List<Object>) map.get("components");
      Optional<Object> optional =
          components
              .stream()
              .filter(
                  config -> {
                    Map<String, Object> m = (Map<String, Object>) config;
                    return "prometheus".equals(m.get("name"));
                  })
              .map(
                  obj -> {
                    Map<String, Object> m = (Map<String, Object>) obj;
                    return m.get("containers");
                  })
              .flatMap(
                  obj -> {
                    List<Object> containers = (List<Object>) obj;
                    return containers.stream();
                  })
              .map(
                  obj -> {
                    Map<String, Object> m = (Map<String, Object>) obj;
                    return m.get("config_files");
                  })
              .flatMap(
                  obj -> {
                    List<Object> configFiles = (List<Object>) obj;
                    return configFiles.stream();
                  })
              .filter(
                  obj -> {
                    Map<String, Object> m = (Map<String, Object>) obj;
                    return "/prometheus_configs/default_prometheus.yml".equals(m.get("filename"));
                  })
              .map(
                  obj -> {
                    Map<String, Object> m = (Map<String, Object>) obj;
                    return m.get("contents");
                  })
              .findFirst();
      if (!optional.isPresent()) {
        throw new RuntimeException();
      }
      return (String) optional.get();
    } catch (RuntimeException e) {
      throw e;
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  @SuppressWarnings("unchecked")
  public String getPriorityRegex(String prometheusYaml) {
    Yaml yaml = new Yaml();
    Map<String, Object> map = yaml.load(prometheusYaml);

    Optional<Object> optional =
        map.values()
            .stream()
            .filter(
                obj -> {
                  return obj instanceof List;
                })
            .flatMap(
                obj -> {
                  List<Object> list = (List<Object>) obj;
                  return list.stream();
                })
            .filter(
                obj -> {
                  return obj instanceof Map;
                })
            .filter(
                obj -> {
                  Map<String, Object> m = (Map<String, Object>) obj;
                  return "yugabyte".equals(m.get("job_name"));
                })
            .map(
                obj -> {
                  Map<String, Object> m = (Map<String, Object>) obj;
                  return m.get("params");
                })
            .map(
                obj -> {
                  Map<String, Object> m = (Map<String, Object>) obj;
                  return m.get("priority_regex");
                })
            .flatMap(
                obj -> {
                  List<Object> regexList = (List<Object>) obj;
                  return regexList.stream();
                })
            .findAny();
    if (!optional.isPresent()) {
      throw new RuntimeException();
    }
    return (String) optional.get();
  }

  @Test
  public void testPrometheusPriorityRegex() {
    String yamlString = getPrometheusYamlString();
    String regex = getPriorityRegex(yamlString);
    Pattern pattern = Pattern.compile(regex);
    Set<String> notMatched = new HashSet<>();
    for (String metric : expectedMetricNames) {
      if (!pattern.matcher(metric).find()) {
        notMatched.add(metric);
      }
    }
    assertTrue("All regex must match. Unmatched regex: " + notMatched, notMatched.isEmpty());
  }
}
