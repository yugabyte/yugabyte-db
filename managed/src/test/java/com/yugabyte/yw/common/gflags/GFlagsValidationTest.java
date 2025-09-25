// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.gflags;

import static org.junit.Assert.*;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.models.Universe;
import java.io.File;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
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
    Map<String, String> connectionPoolingGflagsMap = new HashMap<String, String>();
    connectionPoolingGflagsMap.put("ysql_conn_mgr_max_client_connections", "10001");
    connectionPoolingGflagsMap.put("ysql_conn_mgr_idle_time", "30");
    connectionPoolingGflagsMap.put("ysql_conn_mgr_stats_interval", "20");
    SpecificGFlags connectionPoolingGflagsSpecificGflags =
        SpecificGFlags.construct(connectionPoolingGflagsMap, connectionPoolingGflagsMap);
    Map<UUID, SpecificGFlags> connectionPoolingGflags = new HashMap<UUID, SpecificGFlags>();
    connectionPoolingGflags.put(
        universe.getUniverseDetails().getPrimaryCluster().uuid,
        connectionPoolingGflagsSpecificGflags);
    gFlagsValidation.validateConnectionPoolingGflags(universe, connectionPoolingGflags);

    // Should not throw error for correct new version.
    TestHelper.updateUniverseVersion(universe, "2.25.0.0-b1");
    gFlagsValidation.validateConnectionPoolingGflags(universe, connectionPoolingGflags);

    // Throw error for invalid gflag.
    connectionPoolingGflags
        .get(universe.getUniverseDetails().getPrimaryCluster().uuid)
        .getPerProcessFlags()
        .value
        .get(ServerType.TSERVER)
        .put("random_key", "random_value");
    assertThrows(
        PlatformServiceException.class,
        () -> gFlagsValidation.validateConnectionPoolingGflags(universe, connectionPoolingGflags));

    // Should not throw error for some gflag that starts with prefix "ysql_conn_mgr".
    // Mostly for future cases where DB might add new CP gflags without updating YBA metadata.
    connectionPoolingGflags
        .get(universe.getUniverseDetails().getPrimaryCluster().uuid)
        .getPerProcessFlags()
        .value
        .get(ServerType.TSERVER)
        .remove("random_key");
    connectionPoolingGflags
        .get(universe.getUniverseDetails().getPrimaryCluster().uuid)
        .getPerProcessFlags()
        .value
        .get(ServerType.TSERVER)
        .put("ysql_conn_mgr_future_key", "100");
    gFlagsValidation.validateConnectionPoolingGflags(universe, connectionPoolingGflags);

    connectionPoolingGflags.put(
        UUID.fromString("00000000-0000-0000-0000-000000000000"),
        connectionPoolingGflagsSpecificGflags);
    assertThrows(
        PlatformServiceException.class,
        () -> gFlagsValidation.validateConnectionPoolingGflags(universe, connectionPoolingGflags));
  }
}
