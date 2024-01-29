package com.yugabyte.troubleshoot.ts.service;

import static org.assertj.core.api.Assertions.*;

import com.yugabyte.troubleshoot.ts.models.UniverseMetadata;
import java.util.List;
import java.util.UUID;
import java.util.function.Consumer;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.testcontainers.shaded.com.google.common.collect.ImmutableList;

@ServiceTest
public class UniverseMetadataServiceTest {

  @Autowired private UniverseMetadataService universeMetadataService;

  @Test
  public void testCRUD() {
    UniverseMetadata metadata = testData();
    UniverseMetadata saved = universeMetadataService.save(metadata);
    assertThat(saved).isEqualTo(metadata);

    UniverseMetadata queried = universeMetadataService.get(metadata.getId());
    assertThat(queried).isEqualTo(metadata);
    queried.setApiToken("new_token");

    UniverseMetadata updated = universeMetadataService.save(queried);
    assertThat(updated).isEqualTo(queried);

    List<UniverseMetadata> queriedById =
        universeMetadataService.listByIds(ImmutableList.of(metadata.getId()));
    assertThat(queriedById).containsExactly(updated);

    universeMetadataService.delete(metadata.getId());

    List<UniverseMetadata> remaining = universeMetadataService.listAll();
    assertThat(remaining).isEmpty();
  }

  @Test
  public void testValidation() {
    testValidation(
        metadata -> metadata.setId(null), "Validation failed: {\"id\":[\"must not be null\"]}");

    testValidation(
        metadata -> metadata.setApiToken(null),
        "Validation failed: {\"apiToken\":[\"must not be null\"]}");

    testValidation(
        metadata -> metadata.setPlatformUrl(null),
        "Validation failed: {\"platformUrl\":[\"must not be null\"]}");
    testValidation(
        metadata -> metadata.setPlatformUrl("qwerty"),
        "Validation failed: {\"platformUrl\":[\"must be a valid URL\"]}");

    testValidation(
        metadata -> metadata.setMetricsUrl(null),
        "Validation failed: {\"metricsUrl\":[\"must not be null\"]}");
    testValidation(
        metadata -> metadata.setMetricsUrl("qwerty"),
        "Validation failed: {\"metricsUrl\":[\"must be a valid URL\"]}");
  }

  private void testValidation(Consumer<UniverseMetadata> modifier, String message) {
    UniverseMetadata metadata = testData();
    modifier.accept(metadata);

    assertThatThrownBy(() -> universeMetadataService.save(metadata))
        .isInstanceOf(ValidationException.class)
        .hasMessageContaining(message);
  }

  public static UniverseMetadata testData() {
    return new UniverseMetadata()
        .setId(UUID.randomUUID())
        .setCustomerId(UUID.randomUUID())
        .setApiToken("test_token")
        .setMetricsUrl("http://localhost:9090")
        .setPlatformUrl("http://localhost:9000")
        .setMetricsScrapePeriodSec(10);
  }
}
