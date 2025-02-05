// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.gflags;

import static org.junit.Assert.*;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.models.Universe;
import java.io.File;
import java.util.HashMap;
import java.util.Map;
import junitparams.JUnitParamsRunner;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Spy;
import play.Environment;
import play.Mode;

@Slf4j
@RunWith(JUnitParamsRunner.class)
public class GFlagsValidationTest extends FakeDBApplication {
  private GFlagsValidation gFlagsValidation;
  private Universe universe;
  @Spy Environment environment;

  @Before
  public void setUp() {
    ClassLoader classLoader = Thread.currentThread().getContextClassLoader();
    this.environment = new Environment(new File("."), classLoader, Mode.TEST);
    this.gFlagsValidation = new GFlagsValidation(environment, null, null);
    this.universe = ModelFactory.createUniverse(ModelFactory.testCustomer().getId());
    TestHelper.updateUniverseVersion(universe, "2.23.0.0-b1");
  }

  @Test
  public void testValidateConnectionPoolingGflags() {
    // Should not throw error for valid allowed gflags case.
    Map<String, String> connectionPoolingGflags = new HashMap<String, String>();
    connectionPoolingGflags.put("ysql_conn_mgr_max_client_connections", "10001");
    connectionPoolingGflags.put("ysql_conn_mgr_idle_time", "30");
    connectionPoolingGflags.put("ysql_conn_mgr_stats_interval", "20");
    gFlagsValidation.validateConnectionPoolingGflags(universe, connectionPoolingGflags);

    // Should not throw error for correct new version.
    TestHelper.updateUniverseVersion(universe, "2.25.0.0-b1");
    gFlagsValidation.validateConnectionPoolingGflags(universe, connectionPoolingGflags);

    // Throw error for invalid gflag.
    connectionPoolingGflags.put("random_key", "random_value");
    assertThrows(
        PlatformServiceException.class,
        () -> gFlagsValidation.validateConnectionPoolingGflags(universe, connectionPoolingGflags));

    // Should not throw error for some gflag that starts with prefix "ysql_conn_mgr".
    // Mostly for future cases where DB might add new CP gflags without updating YBA metadata.
    connectionPoolingGflags.remove("random_key");
    connectionPoolingGflags.put("ysql_conn_mgr_future_key", "100");
    gFlagsValidation.validateConnectionPoolingGflags(universe, connectionPoolingGflags);
  }
}
