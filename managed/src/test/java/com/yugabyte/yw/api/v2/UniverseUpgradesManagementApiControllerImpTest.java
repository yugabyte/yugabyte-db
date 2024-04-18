// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.typesafe.config.Config;
import com.yugabyte.yba.v2.client.ApiClient;
import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.Configuration;
import com.yugabyte.yba.v2.client.api.UniverseUpgradesManagementApi;
import com.yugabyte.yba.v2.client.models.PerProcessGFlags;
import com.yugabyte.yba.v2.client.models.SpecificGFlagsPerCluster;
import com.yugabyte.yba.v2.client.models.UniverseGFlags;
import com.yugabyte.yba.v2.client.models.YBPTask;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.CustomWsClientFactoryProvider;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.controllers.UniverseControllerTestBase;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.io.IOException;
import java.util.UUID;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;

public class UniverseUpgradesManagementApiControllerImpTest extends UniverseControllerTestBase {

  private Customer customer;
  private String authToken;

  private static Commissioner mockCommissioner;
  private Config mockConfig;
  private CertificateHelper certificateHelper;
  private AutoFlagUtil mockAutoFlagUtil;
  private XClusterUniverseService mockXClusterUniverseService;

  private Universe defaultUniverse;
  private Universe k8sUniverse;
  private Users user;
  private ApiClient v2ApiClient;

  private final String TMP_CHART_PATH =
      "/tmp/yugaware_tests/" + getClass().getSimpleName() + "/charts";
  private final String TMP_CERTS_PATH = "/tmp/" + getClass().getSimpleName() + "/certs";

  @Mock RuntimeConfigFactory mockRuntimeConfigFactory;

  @Override
  protected Application provideApplication() {
    mockCommissioner = mock(Commissioner.class);
    mockAutoFlagUtil = mock(AutoFlagUtil.class);
    mockXClusterUniverseService = mock(XClusterUniverseService.class);
    ReleaseManager mockReleaseManager = mock(ReleaseManager.class);

    mockConfig = mock(Config.class);
    when(mockConfig.getBoolean("yb.cloud.enabled")).thenReturn(false);
    when(mockConfig.getString("yb.storage.path")).thenReturn("/tmp/" + getClass().getSimpleName());
    when(mockReleaseManager.getReleaseByVersion(any()))
        .thenReturn(
            ReleaseManager.ReleaseMetadata.create("1.0.0")
                .withChartPath(TMP_CHART_PATH + "/uuct_yugabyte-1.0.0-helm.tar.gz"));
    when(mockConfig.getString("yb.security.type")).thenReturn("");
    when(mockConfig.getString("yb.security.clientID")).thenReturn("");
    when(mockConfig.getString("yb.security.secret")).thenReturn("");
    when(mockConfig.getString("yb.security.oidcScope")).thenReturn("");
    when(mockConfig.getString("yb.security.discoveryURI")).thenReturn("");

    when(mockConfig.getInt("yb.fs_stateless.max_files_count_persist")).thenReturn(100);
    when(mockConfig.getBoolean("yb.fs_stateless.suppress_error")).thenReturn(true);
    when(mockConfig.getLong("yb.fs_stateless.max_file_size_bytes")).thenReturn((long) 10000);
    when(mockConfig.getString("ybc.compatible_db_version")).thenReturn("2.15.0.0-b1");
    when(mockRuntimeConfigFactory.globalRuntimeConf()).thenReturn(mockConfig);

    return new GuiceApplicationBuilder()
        .configure(testDatabase())
        .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
        .overrides(
            bind(RuntimeConfigFactory.class)
                .toInstance(new DummyRuntimeConfigFactoryImpl(mockConfig)))
        .overrides(bind(ReleaseManager.class).toInstance(mockReleaseManager))
        .overrides(bind(AutoFlagUtil.class).toInstance(mockAutoFlagUtil))
        .overrides(bind(XClusterUniverseService.class).toInstance(mockXClusterUniverseService))
        .overrides(bind(HealthChecker.class).toInstance(mock(HealthChecker.class)))
        .overrides(
            bind(CustomWsClientFactory.class).toProvider(CustomWsClientFactoryProvider.class))
        .build();
  }

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    authToken = user.createAuthToken();
    certificateHelper = new CertificateHelper(app.injector().instanceOf(RuntimeConfGetter.class));
    new File(TMP_CHART_PATH).mkdirs();
    createTempFile(TMP_CHART_PATH, "uuct_yugabyte-1.0.0-helm.tar.gz", "Sample helm chart data");
    defaultUniverse = ModelFactory.createUniverse("Test Universe2", customer.getId());
    defaultUniverse = ModelFactory.addNodesToUniverse(defaultUniverse.getUniverseUUID(), 3);
    k8sUniverse = ModelFactory.createUniverse("k8s", customer.getId(), Common.CloudType.kubernetes);
    when(mockConfig.hasPath(any())).thenReturn(true);
    when(mockRuntimeConfigFactory.forUniverse(any())).thenReturn(mockConfig);

    v2ApiClient = Configuration.getDefaultApiClient();
    String basePath = String.format("http://localhost:%d/api/v2", port);
    v2ApiClient = v2ApiClient.setBasePath(basePath).addDefaultHeader("X-AUTH-TOKEN", authToken);
    Configuration.setDefaultApiClient(v2ApiClient);
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(TMP_CERTS_PATH));
    FileUtils.deleteDirectory(new File(TMP_CHART_PATH));
  }

  @Test
  public void testGFlagsUpgradeWithValidParams() throws ApiException {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.GFlagsUpgrade);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    UUID universeUUID = createUniverse(customer.getId()).getUniverseUUID();

    PerProcessGFlags perProcessGFlags = new PerProcessGFlags();
    perProcessGFlags.putTserverGflagsItem("tflag1", "123");
    perProcessGFlags.putMasterGflagsItem("mflag1", "456");
    SpecificGFlagsPerCluster specificGFlagsPerCluster =
        new SpecificGFlagsPerCluster().perProcessGflags(perProcessGFlags);
    UniverseGFlags universeGFlags = new UniverseGFlags().primaryGflags(specificGFlagsPerCluster);
    UniverseUpgradesManagementApi api = new UniverseUpgradesManagementApi();
    YBPTask upgradeTask = api.upgradeGFlags(customer.getUuid(), universeUUID, universeGFlags);

    assertEquals(upgradeTask.getTaskUuid(), fakeTaskUUID);

    ArgumentCaptor<GFlagsUpgradeParams> argCaptor =
        ArgumentCaptor.forClass(GFlagsUpgradeParams.class);
    verify(mockCommissioner, times(1)).submit(eq(TaskType.GFlagsUpgrade), argCaptor.capture());

    GFlagsUpgradeParams taskParams = argCaptor.getValue();
    assertEquals(
        "456",
        taskParams
            .getPrimaryCluster()
            .userIntent
            .specificGFlags
            .getPerProcessFlags()
            .value
            .get(ServerType.MASTER)
            .get("mflag1"));
    assertEquals(
        "123",
        taskParams
            .getPrimaryCluster()
            .userIntent
            .specificGFlags
            .getPerProcessFlags()
            .value
            .get(ServerType.TSERVER)
            .get("tflag1"));
    assertEquals(UpgradeOption.ROLLING_UPGRADE, taskParams.upgradeOption);

    // Checking params are merged with universe info.
    Universe universe = Universe.getOrBadRequest(universeUUID);
    assertEquals(universe.getUniverseDetails().rootCA, taskParams.rootCA);
    assertEquals(universe.getUniverseDetails().getClientRootCA(), taskParams.getClientRootCA());
    assertEquals(universe.getUniverseDetails().clusters.size(), taskParams.clusters.size());

    CustomerTask task = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(task);
    assertThat(task.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(task.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(task.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.GFlagsUpgrade)));
    assertAuditEntry(1, customer.getUuid());
  }
}
