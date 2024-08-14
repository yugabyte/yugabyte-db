// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;

import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class XClusterUtilTest extends FakeDBApplication {

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
  }

  @Test
  public void testCheckDbScopedXClusterSupportedSuccess() {
    Universe sourceUniverse = ModelFactory.createUniverse("source Universe");
    TestHelper.updateUniverseVersion(sourceUniverse, "2.23.0.0-b394");
    Universe targetUniverse = ModelFactory.createUniverse("target Universe");
    TestHelper.updateUniverseVersion(targetUniverse, "2.23.0.0-b394");
    try {
      XClusterUtil.checkDbScopedXClusterSupported(sourceUniverse, targetUniverse);
    } catch (Exception e) {
      fail("Source and target universe versions should be valid for db scoped");
    }

    TestHelper.updateUniverseVersion(sourceUniverse, "2024.1.1.0-b50");
    TestHelper.updateUniverseVersion(targetUniverse, "2024.1.1.0-b50");
    try {
      XClusterUtil.checkDbScopedXClusterSupported(sourceUniverse, targetUniverse);
    } catch (Exception e) {
      fail("Source and target universe versions should be valid for db scoped");
    }
  }

  @Test
  public void testCheckDbScopedXClusterSupportedFailure() {
    Universe sourceUniverse = ModelFactory.createUniverse("source Universe");
    Universe targetUniverse = ModelFactory.createUniverse("target Universe");

    // Source universe version does not support db scoped.
    TestHelper.updateUniverseVersion(sourceUniverse, "2.23.0.0-b393");
    TestHelper.updateUniverseVersion(targetUniverse, "2.23.0.0-b394");
    assertThrows(
        PlatformServiceException.class,
        () -> XClusterUtil.checkDbScopedXClusterSupported(sourceUniverse, targetUniverse));

    // Target universe version does not support db scoped.
    TestHelper.updateUniverseVersion(sourceUniverse, "2.23.0.0-b395");
    TestHelper.updateUniverseVersion(targetUniverse, "2.21.0.0-b1");
    assertThrows(
        PlatformServiceException.class,
        () -> XClusterUtil.checkDbScopedXClusterSupported(sourceUniverse, targetUniverse));

    // Source universe version does not support db scoped.
    TestHelper.updateUniverseVersion(sourceUniverse, "2024.1.1.0-b1");
    TestHelper.updateUniverseVersion(targetUniverse, "2024.1.1.0-b52");
    assertThrows(
        PlatformServiceException.class,
        () -> XClusterUtil.checkDbScopedXClusterSupported(sourceUniverse, targetUniverse));

    // Target universe version does not support db scoped.
    TestHelper.updateUniverseVersion(sourceUniverse, "2024.2.1.0-b1");
    TestHelper.updateUniverseVersion(targetUniverse, "2024.1.1.0-b48");
    assertThrows(
        PlatformServiceException.class,
        () -> XClusterUtil.checkDbScopedXClusterSupported(sourceUniverse, targetUniverse));
  }
}
