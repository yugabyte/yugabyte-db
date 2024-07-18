// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.allOf;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yba.v2.client.ApiClient;
import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.Configuration;
import com.yugabyte.yba.v2.client.api.UniverseUpgradesManagementApi;
import com.yugabyte.yba.v2.client.models.ClusterGFlags;
import com.yugabyte.yba.v2.client.models.UpgradeUniverseGFlags;
import com.yugabyte.yba.v2.client.models.UpgradeUniverseGFlags.UpgradeOptionEnum;
import com.yugabyte.yba.v2.client.models.YBPTask;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.CustomWsClientFactoryProvider;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.controllers.UniverseControllerTestBase;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.io.IOException;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
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
  @Mock GFlagsValidation mockGFlagsValidation;

  @Override
  protected Application provideApplication() {
    mockCommissioner = mock(Commissioner.class);
    mockAutoFlagUtil = mock(AutoFlagUtil.class);
    mockXClusterUniverseService = mock(XClusterUniverseService.class);
    mockGFlagsValidation = mock(GFlagsValidation.class);
    ReleaseManager mockReleaseManager = mock(ReleaseManager.class);

    mockConfig = mock(Config.class);
    when(mockConfig.getBoolean("yb.cloud.enabled")).thenReturn(false);
    when(mockConfig.getString("yb.storage.path")).thenReturn("/tmp/" + getClass().getSimpleName());
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create("1.0.0")
            .withChartPath(TMP_CHART_PATH + "/uuct_yugabyte-1.0.0-helm.tar.gz");
    when(mockReleaseManager.getReleaseByVersion(any()))
        .thenReturn(new ReleaseContainer(rm, mockCloudUtilFactory, mockConfig, mockReleasesUtils));
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
        .overrides(bind(GFlagsValidation.class).toInstance(mockGFlagsValidation))
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
    try {
      when(mockGFlagsValidation.getGFlagDetails(anyString(), anyString(), anyString()))
          .thenReturn(Optional.empty());
    } catch (IOException e) {
      fail("Failed to mock gflags validation");
    }

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

  private void runGFlagsUpgrade(UUID universeUUID, UpgradeUniverseGFlags gflags)
      throws ApiException {
    UniverseUpgradesManagementApi api = new UniverseUpgradesManagementApi();
    YBPTask upgradeTask = api.upgradeGFlags(customer.getUuid(), universeUUID, gflags);
  }

  private void verifyNoActions() {
    verify(mockCommissioner, times(0)).submit(any(TaskType.class), any(ITaskParams.class));
    assertAuditEntry(0, customer.getUuid());
  }

  // GFlags upgrade

  @Test
  @Parameters({
    "Ready",
    "UpgradeFailed",
    "Upgrading",
    "PreFinalize",
    "Finalizing",
    "FinalizeFailed",
    "RollingBack",
    "RollbackFailed"
  })
  public void testGFlagsUpgradeWithState(SoftwareUpgradeState state) {
    TestHelper.updateUniverseSoftwareUpgradeState(defaultUniverse, state);
    String primaryCluserUuid =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid.toString();
    UpgradeUniverseGFlags gflags =
        new UpgradeUniverseGFlags()
            .putUniverseGflagsItem(
                primaryCluserUuid,
                new ClusterGFlags().master(Map.of("k1", "v1")).tserver(Map.of("k1", "v1")));
    if (state.equals(SoftwareUpgradeState.Ready)) {
      UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.GFlagsUpgrade);
      when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
      try {
        runGFlagsUpgrade(defaultUniverse.getUniverseUUID(), gflags);
      } catch (ApiException ex) {
        fail("GFlags upgrade for universe in Ready state is expected to succeed");
      }
    } else {
      ApiException exception =
          assertThrows(
              ApiException.class,
              () -> runGFlagsUpgrade(defaultUniverse.getUniverseUUID(), gflags));
      JsonNode ex = Json.parse(exception.getMessage());
      assertEquals(
          "Cannot upgrade gflags on universe in state " + state, ex.get("error").textValue());
      verifyNoActions();
    }
  }

  @Test
  public void testGFlagsUpgradeWithSameFlags() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);

    Universe universe = createUniverse(customer.getId());
    Universe.UniverseUpdater updater =
        universeObject -> {
          UniverseDefinitionTaskParams universeDetails = universeObject.getUniverseDetails();
          UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
          userIntent.specificGFlags =
              SpecificGFlags.construct(
                  ImmutableMap.of("master-flag", "123"), ImmutableMap.of("tserver-flag", "456"));
          universeObject.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(universe.getUniverseUUID(), updater);

    String primaryCluserUuid = universe.getUniverseDetails().getPrimaryCluster().uuid.toString();
    UpgradeUniverseGFlags gflags =
        new UpgradeUniverseGFlags()
            .upgradeOption(UpgradeOptionEnum.NON_ROLLING)
            .putUniverseGflagsItem(
                primaryCluserUuid,
                new ClusterGFlags()
                    .master(Map.of("master-flag", "123"))
                    .tserver(Map.of("tserver-flag", "456")));
    ApiException exception =
        assertThrows(
            ApiException.class, () -> runGFlagsUpgrade(universe.getUniverseUUID(), gflags));
    JsonNode ex = Json.parse(exception.getMessage());
    assertEquals(
        "No changes in gflags (modify specificGflags in cluster)", ex.get("error").asText());

    ArgumentCaptor<GFlagsUpgradeParams> argCaptor =
        ArgumentCaptor.forClass(GFlagsUpgradeParams.class);
    verify(mockCommissioner, times(0)).submit(eq(TaskType.GFlagsUpgrade), argCaptor.capture());

    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testDeleteGFlagsThroughNonRestartOption() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);

    Universe universe = createUniverse(customer.getId());
    // TODO: This test fails if we don't add nodes to this universe! Points to a bug.
    universe = ModelFactory.addNodesToUniverse(universe.getUniverseUUID(), 3);
    Universe.UniverseUpdater updater =
        universeObject -> {
          UniverseDefinitionTaskParams universeDetails = universeObject.getUniverseDetails();
          UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
          userIntent.masterGFlags = ImmutableMap.of("master-flag", "123");
          userIntent.tserverGFlags = ImmutableMap.of("tserver-flag", "456");
          universeObject.setUniverseDetails(universeDetails);
        };
    final Universe universeToTest = Universe.saveDetails(universe.getUniverseUUID(), updater);

    String primaryCluserUuid =
        universeToTest.getUniverseDetails().getPrimaryCluster().uuid.toString();
    UpgradeUniverseGFlags gflags =
        new UpgradeUniverseGFlags()
            .upgradeOption(UpgradeOptionEnum.NON_RESTART)
            .putUniverseGflagsItem(
                primaryCluserUuid,
                new ClusterGFlags()
                    .master(Map.of("master-flag", "123"))
                    .tserver(Map.of("tserver-flag2", "456")));
    ApiException exception =
        assertThrows(
            ApiException.class, () -> runGFlagsUpgrade(universeToTest.getUniverseUUID(), gflags));
    JsonNode ex = Json.parse(exception.getMessage());
    assertEquals(
        "Cannot delete gFlags through non-restart upgrade option.", ex.get("error").asText());

    ArgumentCaptor<GFlagsUpgradeParams> argCaptor =
        ArgumentCaptor.forClass(GFlagsUpgradeParams.class);
    verify(mockCommissioner, times(0)).submit(eq(TaskType.GFlagsUpgrade), argCaptor.capture());

    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.getUuid());
  }

  // @Test
  // cannot construct malformed json body when using the v2 client stubs
  public void testGFlagsUpgradeWithMalformedFlags() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe universe = createUniverse(customer.getId());
    String primaryCluserUuid = universe.getUniverseDetails().getPrimaryCluster().uuid.toString();

    UpgradeUniverseGFlags gflags =
        new UpgradeUniverseGFlags()
            .putUniverseGflagsItem(
                primaryCluserUuid,
                new ClusterGFlags()
                    .master(Map.of("{\"master-flag\": \"123\"", ""))
                    .tserver(Map.of("\"tserver-flag\": \"456\"", "")));
    ApiException exception =
        assertThrows(
            ApiException.class, () -> runGFlagsUpgrade(universe.getUniverseUUID(), gflags));
    JsonNode ex = Json.parse(exception.getMessage());
    assertEquals("JsonProcessingException parsing request body", ex.get("error").asText());

    // String url =
    //     "/api/customers/" + customer.getUuid() + "/universes/" + universeUUID +
    // "/upgrade/gflags";
    // ObjectNode bodyJson = Json.newObject().put("masterGFlags", "abcd").put("tserverGFlags",
    // "abcd");
    // Result result =
    //     assertPlatformException(
    //         () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    // assertBadRequest(result, "JsonProcessingException parsing request body");

    ArgumentCaptor<GFlagsUpgradeParams> argCaptor =
        ArgumentCaptor.forClass(GFlagsUpgradeParams.class);
    verify(mockCommissioner, times(0)).submit(eq(TaskType.GFlagsUpgrade), argCaptor.capture());

    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testGFlagsUpgradeWithValidParams() throws ApiException {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.GFlagsUpgrade);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe createdUniverse = createUniverse(customer.getId());
    UUID universeUUID = createdUniverse.getUniverseUUID();
    String primaryCluserUuid =
        createdUniverse.getUniverseDetails().getPrimaryCluster().uuid.toString();

    ClusterGFlags primaryClusterGFlags =
        new ClusterGFlags().putTserverItem("tflag1", "123").putMasterItem("mflag1", "456");
    UpgradeUniverseGFlags universeGFlags =
        new UpgradeUniverseGFlags().putUniverseGflagsItem(primaryCluserUuid, primaryClusterGFlags);
    UniverseUpgradesManagementApi api = new UniverseUpgradesManagementApi();
    YBPTask upgradeTask = api.upgradeGFlags(customer.getUuid(), universeUUID, universeGFlags);

    assertEquals(fakeTaskUUID, upgradeTask.getTaskUuid());

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
    assertEquals(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, taskParams.upgradeOption);

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

  @Test
  public void testGFlagsUpgradeWithTrimParams() throws ApiException {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.GFlagsUpgrade);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe universe = createUniverse(customer.getId());
    String primaryCluserUuid = universe.getUniverseDetails().getPrimaryCluster().uuid.toString();

    UpgradeUniverseGFlags gflags =
        new UpgradeUniverseGFlags()
            .putUniverseGflagsItem(
                primaryCluserUuid,
                new ClusterGFlags()
                    .master(Map.of("master-flag", " 123 "))
                    .tserver(Map.of("tserver-flag", " 456 ")));
    UniverseUpgradesManagementApi api = new UniverseUpgradesManagementApi();
    YBPTask upgradeTask = api.upgradeGFlags(customer.getUuid(), universe.getUniverseUUID(), gflags);
    assertEquals(fakeTaskUUID, upgradeTask.getTaskUuid());

    ArgumentCaptor<GFlagsUpgradeParams> argCaptor =
        ArgumentCaptor.forClass(GFlagsUpgradeParams.class);
    verify(mockCommissioner, times(1)).submit(eq(TaskType.GFlagsUpgrade), argCaptor.capture());

    GFlagsUpgradeParams taskParams = argCaptor.getValue();
    assertEquals(
        "123",
        taskParams
            .getPrimaryCluster()
            .userIntent
            .specificGFlags
            .getPerProcessFlags()
            .value
            .get(ServerType.MASTER)
            .get("master-flag"));
    assertEquals(
        "456",
        taskParams
            .getPrimaryCluster()
            .userIntent
            .specificGFlags
            .getPerProcessFlags()
            .value
            .get(ServerType.TSERVER)
            .get("tserver-flag"));
    assertEquals(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, taskParams.upgradeOption);

    CustomerTask task = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(task);
    assertThat(task.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(task.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(task.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.GFlagsUpgrade)));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testGFlagsUpgradeNonRolling() throws ApiException {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.GFlagsUpgrade);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe createdUniverse = createUniverse(customer.getId());
    UUID universeUUID = createdUniverse.getUniverseUUID();
    String primaryCluserUuid =
        createdUniverse.getUniverseDetails().getPrimaryCluster().uuid.toString();

    ClusterGFlags primaryClusterGFlags =
        new ClusterGFlags().putTserverItem("tflag1", "123").putMasterItem("mflag1", "456");
    UpgradeUniverseGFlags universeGFlags =
        new UpgradeUniverseGFlags().putUniverseGflagsItem(primaryCluserUuid, primaryClusterGFlags);
    universeGFlags.upgradeOption(UpgradeOptionEnum.NON_ROLLING);
    UniverseUpgradesManagementApi api = new UniverseUpgradesManagementApi();
    YBPTask upgradeTask = api.upgradeGFlags(customer.getUuid(), universeUUID, universeGFlags);

    assertEquals(fakeTaskUUID, upgradeTask.getTaskUuid());

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
    assertEquals(UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE, taskParams.upgradeOption);

    CustomerTask task = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(task);
    assertThat(task.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(task.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(task.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.GFlagsUpgrade)));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testGFlagsUpgradeWithKubernetesUniverse() throws ApiException {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.GFlagsKubernetesUpgrade);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe universe = createUniverse("Test Universe", customer.getId(), CloudType.kubernetes);
    Map<String, String> universeConfig = new HashMap<>();
    universeConfig.put(Universe.HELM2_LEGACY, "helm");
    universe.setConfig(universeConfig);
    universe.save();
    String primaryCluserUuid = universe.getUniverseDetails().getPrimaryCluster().uuid.toString();

    UpgradeUniverseGFlags gflags =
        new UpgradeUniverseGFlags()
            .putUniverseGflagsItem(
                primaryCluserUuid,
                new ClusterGFlags()
                    .master(Map.of("master-flag", "123"))
                    .tserver(Map.of("tserver-flag", "456")));
    UniverseUpgradesManagementApi api = new UniverseUpgradesManagementApi();
    YBPTask upgradeTask = api.upgradeGFlags(customer.getUuid(), universe.getUniverseUUID(), gflags);
    assertEquals(fakeTaskUUID, upgradeTask.getTaskUuid());

    ArgumentCaptor<GFlagsUpgradeParams> argCaptor =
        ArgumentCaptor.forClass(GFlagsUpgradeParams.class);
    verify(mockCommissioner, times(1))
        .submit(eq(TaskType.GFlagsKubernetesUpgrade), argCaptor.capture());

    GFlagsUpgradeParams taskParams = argCaptor.getValue();
    assertEquals(
        "123",
        taskParams
            .getPrimaryCluster()
            .userIntent
            .specificGFlags
            .getPerProcessFlags()
            .value
            .get(ServerType.MASTER)
            .get("master-flag"));
    assertEquals(
        "456",
        taskParams
            .getPrimaryCluster()
            .userIntent
            .specificGFlags
            .getPerProcessFlags()
            .value
            .get(ServerType.TSERVER)
            .get("tserver-flag"));
    assertEquals(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, taskParams.upgradeOption);

    CustomerTask task = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(task);
    assertThat(task.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(task.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(task.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.GFlagsUpgrade)));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testGflagsUpgradeSameParamsSpecificGFlags() {
    SpecificGFlags gFlags =
        SpecificGFlags.construct(Map.of("master-flag", "1"), Map.of("tserver-flag", "2"));
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags = gFlags;
            });
    ApiException exception =
        assertThrows(
            ApiException.class,
            () -> runGFlagsUpgrade(defaultUniverse.getUniverseUUID(), new UpgradeUniverseGFlags()));
    JsonNode ex = Json.parse(exception.getMessage());
    assertEquals(
        "No changes in gflags (modify specificGflags in cluster)", ex.get("error").asText());
    verifyNoActions();
  }

  @Test
  public void testGflagsUpgradeDeleteNonRestartSpecificGFlags() {
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              universe.getUniverseDetails().getPrimaryCluster().userIntent.masterGFlags =
                  Map.of("master-flag", "1");
              universe.getUniverseDetails().getPrimaryCluster().userIntent.tserverGFlags =
                  Map.of("tserver-flag", "2");
            });
    String primaryCluserUuid =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid.toString();

    UpgradeUniverseGFlags gflags =
        new UpgradeUniverseGFlags()
            .upgradeOption(UpgradeOptionEnum.NON_RESTART)
            .putUniverseGflagsItem(
                primaryCluserUuid,
                new ClusterGFlags()
                    .master(Map.of("master-flag2", "2"))
                    .tserver(Map.of("tserver-flag", "2")));
    ApiException exception =
        assertThrows(
            ApiException.class, () -> runGFlagsUpgrade(defaultUniverse.getUniverseUUID(), gflags));
    JsonNode ex = Json.parse(exception.getMessage());
    assertEquals(
        "Cannot delete gFlags through non-restart upgrade option.", ex.get("error").asText());
    verifyNoActions();
  }
}
