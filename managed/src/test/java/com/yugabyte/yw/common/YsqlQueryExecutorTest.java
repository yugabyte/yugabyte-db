package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.google.common.collect.ImmutableList;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.CustomWsClientFactoryProvider;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.DatabaseUserFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import kamon.instrumentation.play.GuiceModule;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.test.WithApplication;

@RunWith(JUnitParamsRunner.class)
public class YsqlQueryExecutorTest extends WithApplication {

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

  @Test
  @Parameters({"false, 200", "true, 500", "true, 400"})
  public void testExecution(boolean failure, int errorCode) {
    RuntimeConfigFactory mockRuntimeConfigFactory = mock(RuntimeConfigFactory.class);
    when(mockRuntimeConfigFactory.forUniverse(any())).thenReturn(mockRuntimeConfig);
    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);
    YsqlQueryExecutor ysqlQueryExecutor =
        spy(new YsqlQueryExecutor(mockRuntimeConfigFactory, mockNodeUniverseManager));
    Universe universe = mock(Universe.class);
    when(universe.getVersions()).thenReturn(ImmutableList.of("2.15.0.0-b1"));
    UniverseDefinitionTaskParams details = mock(UniverseDefinitionTaskParams.class);
    when(universe.getUniverseDetails()).thenReturn(details);
    Cluster cluster =
        new Cluster(
            UniverseDefinitionTaskParams.ClusterType.PRIMARY,
            new UniverseDefinitionTaskParams.UserIntent());
    when(details.getPrimaryCluster()).thenReturn(new Cluster(null, null));
    NodeDetails node = new NodeDetails();
    node.isMaster = true;
    node.isTserver = true;
    when(universe.getMasterLeaderNode()).thenReturn(errorCode == 500 ? null : node);
    ShellResponse failureResponse = new ShellResponse();
    failureResponse.code = 1;
    failureResponse.message = "Failed!";
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), any(), any()))
        .thenReturn(errorCode == 400 ? failureResponse : new ShellResponse());
    DatabaseUserFormData dbForm = new DatabaseUserFormData();
    dbForm.dbName = "yugabyte";
    dbForm.password = "admin";
    dbForm.username = "admin";
    dbForm.ycqlAdminPassword = "cassandra";
    dbForm.ycqlAdminUsername = "cassandra";
    dbForm.ysqlAdminPassword = "yugabyte";
    dbForm.ysqlAdminUsername = "yugabyte";
    if (failure) {
      PlatformServiceException exception =
          assertThrows(
              PlatformServiceException.class, () -> ysqlQueryExecutor.createUser(universe, dbForm));
      assertEquals(errorCode, exception.getHttpStatus());
    } else {
      ysqlQueryExecutor.createUser(universe, dbForm);
    }
  }
}
