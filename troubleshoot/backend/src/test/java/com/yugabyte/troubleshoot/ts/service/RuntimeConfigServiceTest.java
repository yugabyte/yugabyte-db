package com.yugabyte.troubleshoot.ts.service;

import static org.assertj.core.api.Assertions.assertThat;

import com.google.common.collect.ImmutableList;
import com.yugabyte.troubleshoot.ts.models.*;
import java.util.List;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;

@ServiceTest
public class RuntimeConfigServiceTest {

  private @Autowired RuntimeConfigService runtimeConfigService;
  private UniverseMetadata metadata = UniverseMetadataServiceTest.testData();

  @Test
  public void testCRUD() {
    RuntimeConfig config = runtimeConfigService.getUniverseConfig(metadata);
    long value = config.getLong(RuntimeConfigKey.QUERY_LATENCY_BATCH_SIZE);
    assertThat(value).isEqualTo(10);

    runtimeConfigService.save(testData(metadata));

    config = runtimeConfigService.getUniverseConfig(metadata);
    value = config.getLong(RuntimeConfigKey.QUERY_LATENCY_BATCH_SIZE);
    assertThat(value).isEqualTo(20);

    runtimeConfigService.delete(
        new RuntimeConfigEntryKey(metadata.getId(), "anomaly.query_latency.batch_size"));
    config = runtimeConfigService.getUniverseConfig(metadata);
    value = config.getLong(RuntimeConfigKey.QUERY_LATENCY_BATCH_SIZE);
    assertThat(value).isEqualTo(15);
  }

  public static List<RuntimeConfigEntry> testData(UniverseMetadata metadata) {
    return ImmutableList.of(
        new RuntimeConfigEntry(metadata.getId(), "anomaly.query_latency.batch_size", "20"),
        new RuntimeConfigEntry(metadata.getId(), "anomaly.query_latency.min_anomaly_value", "1"),
        new RuntimeConfigEntry(metadata.getCustomerId(), "anomaly.query_latency.batch_size", "15"));
  }
}
