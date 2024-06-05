// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.troubleshooting;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.*;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.TroubleshootingPlatform;
import com.yugabyte.yw.models.filters.TroubleshootingPlatformFilter;
import java.util.List;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class TroubleshootingPlatformServiceTest extends FakeDBApplication {

  private UUID defaultCustomerUuid;

  private TroubleshootingPlatformService troubleshootingPlatformService;

  @Before
  public void setUp() {
    defaultCustomerUuid = ModelFactory.testCustomer().getUuid();
    troubleshootingPlatformService =
        app.injector().instanceOf(TroubleshootingPlatformService.class);
  }

  @Test
  public void testCreateAndGet() {
    TroubleshootingPlatform platform = createTestPlatform("https://tp.yugabyte.com:8443");
    TroubleshootingPlatform updated = troubleshootingPlatformService.save(platform, false);

    assertThat(updated, equalTo(platform));

    TroubleshootingPlatform fromDb =
        troubleshootingPlatformService.get(platform.getCustomerUUID(), platform.getUuid());
    assertThat(fromDb, equalTo(platform));
  }

  @Test
  public void testGetOrBadRequest() {
    // Should raise an exception for random UUID.
    final UUID uuid = UUID.randomUUID();
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              troubleshootingPlatformService.getOrBadRequest(defaultCustomerUuid, uuid);
            });
    assertThat(exception.getMessage(), equalTo("Troubleshooting Platform not found"));
  }

  @Test
  public void testListByCustomerUuid() {
    TroubleshootingPlatform platform = createTestPlatform("https://tp.yugabyte.com:8443");
    troubleshootingPlatformService.save(platform, false);

    TroubleshootingPlatform platform2 = createTestPlatform("https://tp2.yugabyte.com:8443");
    troubleshootingPlatformService.save(platform2, false);

    UUID newCustomerUUID = ModelFactory.testCustomer().getUuid();
    TroubleshootingPlatform otherCustomerPlatform =
        createTestPlatform(newCustomerUUID, "https://tp3.yugabyte.com:8443");

    TroubleshootingPlatformFilter filter =
        TroubleshootingPlatformFilter.builder().customerUuid(defaultCustomerUuid).build();
    List<TroubleshootingPlatform> platforms = troubleshootingPlatformService.list(filter);
    assertThat(platforms, containsInAnyOrder(platform, platform2));
  }

  @Test
  public void testValidateDuplicateUrl() {
    TroubleshootingPlatform platform = createTestPlatform("https://tp.yugabyte.com:8443");
    troubleshootingPlatformService.save(platform, false);

    TroubleshootingPlatform duplicate = createTestPlatform("https://tp.yugabyte.com:8443");
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              troubleshootingPlatformService.save(duplicate, false);
            });
    assertThat(
        exception.getMessage(),
        equalTo("errorJson: {\"tpUrl\":[\"platform with such url already exists.\"]}"));
  }

  @Test
  public void testDelete() {
    TroubleshootingPlatform platform = createTestPlatform("https://tp.yugabyte.com:8443");
    troubleshootingPlatformService.save(platform, false);

    troubleshootingPlatformService.delete(platform.getCustomerUUID(), platform.getUuid(), true);

    TroubleshootingPlatform fromDb =
        troubleshootingPlatformService.get(platform.getCustomerUUID(), platform.getUuid());
    assertThat(fromDb, nullValue());
  }

  private TroubleshootingPlatform createTestPlatform(String name) {
    return createTestPlatform(defaultCustomerUuid, name);
  }

  public static TroubleshootingPlatform createTestPlatform(UUID customerUUID, String tpUrl) {
    TroubleshootingPlatform platform = new TroubleshootingPlatform();
    platform.setCustomerUUID(customerUUID);
    platform.setTpUrl(tpUrl);
    platform.setYbaUrl("http://localhost:9000");
    platform.setMetricsUrl("http://localhost:9090");
    return platform;
  }
}
