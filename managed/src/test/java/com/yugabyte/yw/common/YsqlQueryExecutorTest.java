package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.google.common.collect.ImmutableList;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.DatabaseUserDropFormData;
import com.yugabyte.yw.forms.DatabaseUserFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import kamon.instrumentation.play.GuiceModule;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;

@RunWith(JUnitParamsRunner.class)
public class YsqlQueryExecutorTest extends PlatformGuiceApplicationBaseTest {

  protected Config mockRuntimeConfig;
  protected NodeUniverseManager mockNodeUniverseManager;
  protected HealthChecker healthChecker;

  @Override
  protected Application provideApplication() {
    mockRuntimeConfig = mock(Config.class);
    mockNodeUniverseManager = mock(NodeUniverseManager.class);
    healthChecker = mock(HealthChecker.class);
    return new GuiceApplicationBuilder()
        .disable(GuiceModule.class)
        .configure(testDatabase())
        .overrides(bind(NodeUniverseManager.class).toInstance(mockNodeUniverseManager))
        .overrides(bind(HealthChecker.class).toInstance(healthChecker))
        .overrides(
            bind(RuntimeConfigFactory.class)
                .toInstance(new DummyRuntimeConfigFactoryImpl(mockRuntimeConfig)))
        .overrides(
            bind(CustomWsClientFactory.class).toProvider(CustomWsClientFactoryProvider.class))
        .build();
  }

  protected RuntimeConfigFactory mockRuntimeConfigFactory;
  protected YsqlQueryExecutor ysqlQueryExecutor;
  protected Universe universe;
  protected UniverseDefinitionTaskParams details;
  protected Cluster cluster;
  protected NodeDetails node;
  protected ShellResponse failureResponse;
  protected DatabaseUserFormData dbForm;

  @Before
  public void setUp() {
    mockRuntimeConfigFactory = mock(RuntimeConfigFactory.class);
    when(mockRuntimeConfigFactory.forUniverse(any())).thenReturn(mockRuntimeConfig);
    when(mockRuntimeConfigFactory.forCustomer(any())).thenReturn(mockRuntimeConfig);
    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);
    when(mockRuntimeConfig.getLong("yb.ysql_timeout_secs")).thenReturn(180L);

    ysqlQueryExecutor =
        spy(new YsqlQueryExecutor(mockRuntimeConfigFactory, mockNodeUniverseManager));

    universe = mock(Universe.class);
    when(universe.getVersions()).thenReturn(ImmutableList.of("2.15.0.0-b1"));

    details = mock(UniverseDefinitionTaskParams.class);
    when(universe.getUniverseDetails()).thenReturn(details);

    cluster =
        new Cluster(
            UniverseDefinitionTaskParams.ClusterType.PRIMARY,
            new UniverseDefinitionTaskParams.UserIntent());
    when(details.getPrimaryCluster())
        .thenReturn(
            new Cluster(
                UniverseDefinitionTaskParams.ClusterType.PRIMARY,
                new UniverseDefinitionTaskParams.UserIntent()));

    node = new NodeDetails();
    node.isMaster = true;
    node.isTserver = true;

    failureResponse = new ShellResponse();
    failureResponse.code = 1;
    failureResponse.message = "Failed!";

    dbForm = new DatabaseUserFormData();
    dbForm.dbName = "yugabyte";
    dbForm.password = "admin";
    dbForm.username = "admin";
    dbForm.ycqlAdminPassword = "cassandra";
    dbForm.ycqlAdminUsername = "cassandra";
    dbForm.ysqlAdminPassword = "yugabyte";
    dbForm.ysqlAdminUsername = "yugabyte";
  }

  @Test
  @Parameters({"false, 200", "true, 500", "true, 400"})
  public void createUser(boolean failure, int errorCode) {
    when(universe.getMasterLeaderNode()).thenReturn(errorCode == 500 ? null : node);
    when(mockNodeUniverseManager.runYsqlCommand(
            any(), any(), any(), any(), anyLong(), anyBoolean()))
        .thenReturn(errorCode == 400 ? failureResponse : new ShellResponse());
    if (failure) {
      PlatformServiceException exception =
          assertThrows(
              PlatformServiceException.class, () -> ysqlQueryExecutor.createUser(universe, dbForm));
      assertEquals(errorCode, exception.getHttpStatus());
    } else {
      ysqlQueryExecutor.createUser(universe, dbForm);
    }
  }

  @Test
  @Parameters({"false, 200", "true, 500", "true, 400"})
  public void createRestrictedUser(boolean failure, int errorCode) {
    when(universe.getMasterLeaderNode()).thenReturn(errorCode == 500 ? null : node);
    when(mockNodeUniverseManager.runYsqlCommand(
            any(), any(), any(), any(), anyLong(), anyBoolean()))
        .thenReturn(errorCode == 400 ? failureResponse : new ShellResponse());
    if (failure) {
      PlatformServiceException exception =
          assertThrows(
              PlatformServiceException.class,
              () -> ysqlQueryExecutor.createRestrictedUser(universe, dbForm));
      assertEquals(errorCode, exception.getHttpStatus());
    } else {
      ysqlQueryExecutor.createRestrictedUser(universe, dbForm);
    }
  }

  @Test
  @Parameters({"false, 200", "true, 500", "true, 400"})
  public void deleteUser(boolean failure, int errorCode) {
    DatabaseUserDropFormData dropForm = new DatabaseUserDropFormData();
    dropForm.username = "admin";
    dropForm.dbName = "yugabyte";

    when(universe.getMasterLeaderNode()).thenReturn(errorCode == 500 ? null : node);
    when(mockNodeUniverseManager.runYsqlCommand(
            any(), any(), any(), any(), anyLong(), anyBoolean()))
        .thenReturn(errorCode == 400 ? failureResponse : new ShellResponse());
    if (failure) {
      PlatformServiceException exception =
          assertThrows(
              PlatformServiceException.class, () -> ysqlQueryExecutor.dropUser(universe, dropForm));
      assertEquals(errorCode, exception.getHttpStatus());
    } else {
      ysqlQueryExecutor.dropUser(universe, dropForm);
    }
  }

  @Test
  @Parameters({"yugabyte", "postgres", "yb_superuser"})
  public void dropSystemUser(String username) {
    DatabaseUserDropFormData dropForm = new DatabaseUserDropFormData();
    dropForm.username = username;
    dropForm.dbName = "yugabyte";
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class, () -> ysqlQueryExecutor.dropUser(universe, dropForm));
    assertEquals(400, exception.getHttpStatus());
  }
}
